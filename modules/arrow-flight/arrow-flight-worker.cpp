/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Attila Szakacs <attila.szakacs@axoflow.com>
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#include "arrow-flight-worker.hpp"

#include "compat/cpp-start.h"
#include "logthrdest/logthrdestdrv.h"
#include "scratch-buffers.h"
#include "messages.h"
#include "logmsg/type-hinting.h"
#include "compat/cpp-end.h"

#include <arrow/api.h>
#include <arrow/array/builder_primitive.h>
#include <arrow/array/builder_binary.h>
#include <arrow/flight/api.h>

using syslog_ng::arrow_flight::DestinationDriver;
using syslog_ng::arrow_flight::DestinationWorker;

struct _ArrowFlightDestWorker
{
  LogThreadedDestWorker super;
  DestinationWorker *cpp;
};


DestinationWorker::DestinationWorker(ArrowFlightDestWorker *s) : super(s)
{
}

DestinationWorker::~DestinationWorker()
{
}

DestinationDriver *
DestinationWorker::get_owner()
{
  return arrow_flight_dd_get_cpp(&this->super->super.owner->super.super);
}

bool
DestinationWorker::init()
{
  DestinationDriver *owner = this->get_owner();

  auto location_result = arrow::flight::Location::Parse(owner->get_url());
  if (!location_result.ok())
    {
      msg_error("arrow-flight: Failed to parse URL",
                evt_tag_str("url", owner->get_url().c_str()),
                evt_tag_str("error", location_result.status().ToString().c_str()),
                log_pipe_location_tag(&this->super->super.owner->super.super.super));
      return false;
    }
  this->location = *location_result;

  return log_threaded_dest_worker_init_method(&this->super->super);
}

void
DestinationWorker::deinit()
{
  log_threaded_dest_worker_deinit_method(&this->super->super);
}

bool
DestinationWorker::connect()
{
  DestinationDriver *owner = this->get_owner();

  auto client_result = arrow::flight::FlightClient::Connect(this->location);
  if (!client_result.ok())
    {
      msg_error("arrow-flight: Failed to connect to server",
                evt_tag_str("url", owner->get_url().c_str()),
                evt_tag_str("error", client_result.status().ToString().c_str()),
                log_pipe_location_tag(&this->super->super.owner->super.super.super));
      return false;
    }
  this->client = std::move(*client_result);

  return this->create_builders();
}

void
DestinationWorker::disconnect()
{
  this->close_stream();

  if (this->client)
    {
      auto status = this->client->Close();
      if (!status.ok())
        msg_warning("arrow-flight: Error closing client connection",
                    evt_tag_str("error", status.ToString().c_str()),
                    log_pipe_location_tag(&this->super->super.owner->super.super.super));
      this->client.reset();
    }
  this->builders.clear();
}

bool
DestinationWorker::create_builders()
{
  DestinationDriver *owner = this->get_owner();
  const auto &arrow_schema = owner->get_arrow_schema();
  this->builders.clear();
  for (int i = 0; i < arrow_schema->num_fields(); i++)
    {
      auto builder_result = arrow::MakeBuilder(arrow_schema->field(i)->type());
      if (!builder_result.ok())
        {
          msg_error("arrow-flight: Failed to create Arrow builder",
                    evt_tag_str("field", arrow_schema->field(i)->name().c_str()),
                    evt_tag_str("error", builder_result.status().ToString().c_str()),
                    log_pipe_location_tag(&this->super->super.owner->super.super.super));
          return false;
        }
      this->builders.push_back(std::move(*builder_result));
    }
  return true;
}

bool
DestinationWorker::append_value(arrow::ArrayBuilder *builder, const std::shared_ptr<arrow::DataType> &type,
                                const char *str, gssize len)
{
  arrow::Status s;

  switch (type->id())
    {
    case arrow::Type::STRING:
      s = static_cast<arrow::StringBuilder *>(builder)->Append(str, (int32_t) len);
      break;
    case arrow::Type::INT64:
    {
      gint64 v;
      if (!type_cast_to_int64(str, len, &v, NULL))
        return false;
      s = static_cast<arrow::Int64Builder *>(builder)->Append((int64_t) v);
      break;
    }
    case arrow::Type::DOUBLE:
    {
      gdouble v;
      if (!type_cast_to_double(str, len, &v, NULL))
        return false;
      s = static_cast<arrow::DoubleBuilder *>(builder)->Append((double) v);
      break;
    }
    case arrow::Type::BOOL:
    {
      gboolean v;
      if (!type_cast_to_boolean(str, len, &v, NULL))
        return false;
      s = static_cast<arrow::BooleanBuilder *>(builder)->Append((bool) v);
      break;
    }
    case arrow::Type::TIMESTAMP:
    {
      gint64 v;
      if (!type_cast_to_int64(str, len, &v, NULL))
        return false;
      s = static_cast<arrow::TimestampBuilder *>(builder)->Append((int64_t) v);
      break;
    }
    default:
      return false;
    }

  return s.ok();
}

LogThreadedResult
DestinationWorker::insert(LogMessage *msg)
{
  return LTR_QUEUED;
}

const gchar *
DestinationWorker::format_path(LogMessage *msg, GString **buf, ScratchBuffersMarker *marker)
{
  *buf = nullptr;
  LogTemplate *path_template = this->get_owner()->get_path_template();
  if (!path_template)
    return "";
  gssize len;
  LogMessageValueType vtype;
  if (log_template_is_trivial(path_template))
    {
      const gchar *path = log_template_get_trivial_value_and_type(path_template, msg, &len, &vtype);
      if (len < 0 || vtype == LM_VT_NULL)
        return "";
      return path;
    }
  *buf = scratch_buffers_alloc_and_mark(marker);
  LogTemplateEvalOptions opts = {&this->get_owner()->get_template_options(), LTZ_SEND,
                                 this->super->super.seq_num, NULL, LM_VT_STRING
                                };
  log_template_format(path_template, msg, &opts, *buf);
  return (*buf)->str;
}

bool
DestinationWorker::open_stream(const gchar *path)
{
  this->close_stream();

  DestinationDriver *owner = this->get_owner();

  auto descriptor = arrow::flight::FlightDescriptor::Path({path});
  auto put_result = this->client->DoPut(arrow::flight::FlightCallOptions{}, descriptor, owner->get_arrow_schema());
  if (!put_result.ok())
    {
      msg_error("arrow-flight: DoPut failed",
                evt_tag_str("error", put_result.status().ToString().c_str()),
                log_pipe_location_tag(&this->super->super.owner->super.super.super));
      return false;
    }

  this->writer = std::move(put_result->writer);
  this->metadata_reader = std::move(put_result->reader);
  this->current_stream_path = path;

  return true;
}

void
DestinationWorker::close_stream()
{
  if (!this->writer)
    return;

  auto s = this->writer->DoneWriting();
  if (!s.ok())
    msg_warning("arrow-flight: Error closing stream",
                evt_tag_str("error", s.ToString().c_str()),
                log_pipe_location_tag(&this->super->super.owner->super.super.super));

  s = this->writer->Close();
  if (!s.ok())
    msg_warning("arrow-flight: Error closing stream",
                evt_tag_str("error", s.ToString().c_str()),
                log_pipe_location_tag(&this->super->super.owner->super.super.super));

  this->writer.reset();
  this->metadata_reader.reset();
  this->current_stream_path.clear();
}

LogThreadedResult
DestinationWorker::flush(LogThreadedFlushMode mode)
{
  return LTR_SUCCESS;
}


/* C Wrappers */

static LogThreadedResult
_insert(LogThreadedDestWorker *s, LogMessage *msg)
{
  ArrowFlightDestWorker *self = (ArrowFlightDestWorker *) s;
  return self->cpp->insert(msg);
}

static LogThreadedResult
_flush(LogThreadedDestWorker *s, LogThreadedFlushMode mode)
{
  ArrowFlightDestWorker *self = (ArrowFlightDestWorker *) s;
  return self->cpp->flush(mode);
}

static gboolean
_init(LogThreadedDestWorker *s)
{
  ArrowFlightDestWorker *self = (ArrowFlightDestWorker *) s;
  return self->cpp->init();
}

static void
_deinit(LogThreadedDestWorker *s)
{
  ArrowFlightDestWorker *self = (ArrowFlightDestWorker *) s;
  self->cpp->deinit();
}

static gboolean
_connect(LogThreadedDestWorker *s)
{
  ArrowFlightDestWorker *self = (ArrowFlightDestWorker *) s;
  return self->cpp->connect();
}

static void
_disconnect(LogThreadedDestWorker *s)
{
  ArrowFlightDestWorker *self = (ArrowFlightDestWorker *) s;
  self->cpp->disconnect();
}

static void
_free(LogThreadedDestWorker *s)
{
  ArrowFlightDestWorker *self = (ArrowFlightDestWorker *) s;
  delete self->cpp;

  log_threaded_dest_worker_free_method(s);
}

LogThreadedDestWorker *
arrow_flight_dw_new(LogThreadedDestDriver *o, gint worker_index)
{
  ArrowFlightDestWorker *self = g_new0(ArrowFlightDestWorker, 1);

  log_threaded_dest_worker_init_instance(&self->super, o, worker_index);

  self->cpp = new DestinationWorker(self);

  self->super.init = _init;
  self->super.deinit = _deinit;
  self->super.connect = _connect;
  self->super.disconnect = _disconnect;
  self->super.insert = _insert;
  self->super.flush = _flush;
  self->super.free_fn = _free;

  return &self->super;
}
