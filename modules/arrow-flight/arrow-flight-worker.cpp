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
#include "messages.h"
#include "logmsg/type-hinting.h"
#include "template/templates.h"
#include <json.h>
#include "compat/cpp-end.h"

#include <arrow/api.h>
#include <arrow/array/builder_primitive.h>
#include <arrow/array/builder_binary.h>
#include <arrow/flight/api.h>
#include <arrow/ipc/writer.h>

using syslog_ng::arrow_flight::DestinationDriver;
using syslog_ng::arrow_flight::DestinationWorker;

struct _ArrowFlightDestWorker
{
  LogThreadedDestWorker super;
  DestinationWorker *cpp;
};


DestinationWorker::DestinationWorker(ArrowFlightDestWorker *s) : super(s)
{
  this->buffer = g_string_new(NULL);
}

DestinationWorker::~DestinationWorker()
{
  g_string_free(this->buffer, TRUE);
}

DestinationDriver *
DestinationWorker::get_owner()
{
  return arrow_flight_dd_get_cpp(&this->super->super.owner->super.super);
}

gint
DestinationWorker::get_batch_size() const
{
  return this->super->super.batch_size;
}

bool
DestinationWorker::should_initiate_flush()
{
  size_t batch_bytes = this->get_owner()->get_batch_bytes();
  return batch_bytes > 0 && this->current_batch_bytes >= batch_bytes;
}

void
DestinationWorker::prepare_batch()
{
  this->current_batch_bytes = 0;
  this->current_batch_path.clear();
}

bool
DestinationWorker::init()
{
  DestinationDriver *owner = this->get_owner();

  const std::string &raw_url = owner->get_url();
  std::string url = (raw_url.find("://") == std::string::npos) ? "grpc://" + raw_url : raw_url;

  auto location_result = arrow::flight::Location::Parse(url);
  if (!location_result.ok())
    {
      msg_error("arrow-flight: Failed to parse URL",
                evt_tag_str("url", url.c_str()),
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
  (void) this->close_stream();
  this->current_batch_path.clear();

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

/* This function downcasts the builder. Make sure to pass one that matches the type. */
gssize
DestinationWorker::append_map_string_string(arrow::MapBuilder *mbuilder, const char *str, gssize len)
{
  auto *key_builder = static_cast<arrow::StringBuilder *>(mbuilder->key_builder());
  auto *item_builder = static_cast<arrow::StringBuilder *>(mbuilder->item_builder());

  struct json_tokener *tok = json_tokener_new();
  struct json_object *jso = json_tokener_parse_ex(tok, str, (int) len);
  enum json_tokener_error jerr = json_tokener_get_error(tok);
  json_tokener_free(tok);

  if (!jso || jerr != json_tokener_success || !json_object_is_type(jso, json_type_object))
    {
      if (jso)
        json_object_put(jso);
      return -1;
    }

  int64_t prev_key_bytes = key_builder->value_data_length();
  int64_t prev_item_bytes = item_builder->value_data_length();
  int64_t entries = 0;
  arrow::Status s = mbuilder->Append();

  if (s.ok())
    {
      struct json_object_iter itr;
      json_object_object_foreachC(jso, itr)
      {
        s = key_builder->Append(itr.key, (int32_t) strlen(itr.key));
        if (!s.ok())
          break;

        const char *v;
        size_t v_len;
        if (json_object_is_type(itr.val, json_type_string))
          {
            v = json_object_get_string(itr.val);
            v_len = (size_t) json_object_get_string_len(itr.val);
          }
        else
          {
            v = json_object_to_json_string_ext(itr.val, JSON_C_TO_STRING_PLAIN);
            v_len = v ? strlen(v) : 0;
          }
        s = item_builder->Append(v, (int32_t) v_len);
        if (!s.ok())
          break;
        entries++;
      }
    }
  json_object_put(jso);

  if (!s.ok())
    return -1;

  /*
   * Approximate byte cost: key + item data growth, one offset per entry in each inner
   * StringArray, plus one offset for the map slot itself.
   */
  return (key_builder->value_data_length() - prev_key_bytes) +
         (item_builder->value_data_length() - prev_item_bytes) +
         entries * 2 * (gssize) sizeof(int32_t) +
         (gssize) sizeof(int32_t);
}

gssize
DestinationWorker::append_value(arrow::ArrayBuilder *builder, const std::shared_ptr<arrow::DataType> &type,
                                const char *str, gssize len)
{
  arrow::Status s;
  gssize serialized_len = 0;

  switch (type->id())
    {
    case arrow::Type::STRING:
    {
      /* StringArray stores variable-length values: data buffer growth + a 4-byte offset entry per element. */
      auto *sbuilder = static_cast<arrow::StringBuilder *>(builder);
      int64_t prev_bytes = sbuilder->value_data_length();
      s = sbuilder->Append(str, (int32_t) len);
      serialized_len = (sbuilder->value_data_length() - prev_bytes) + (gssize) sizeof(int32_t);
      break;
    }
    case arrow::Type::INT64:
    {
      gint64 v;
      if (!type_cast_to_int64(str, len, &v, NULL))
        return -1;
      s = static_cast<arrow::Int64Builder *>(builder)->Append((int64_t) v);
      serialized_len = sizeof(int64_t);
      break;
    }
    case arrow::Type::DOUBLE:
    {
      gdouble v;
      if (!type_cast_to_double(str, len, &v, NULL))
        return -1;
      s = static_cast<arrow::DoubleBuilder *>(builder)->Append((double) v);
      serialized_len = sizeof(double);
      break;
    }
    case arrow::Type::BOOL:
    {
      gboolean v;
      if (!type_cast_to_boolean(str, len, &v, NULL))
        return -1;
      /* BooleanArray is bit-packed: data buffer grows by one byte only when crossing an 8-element boundary. */
      auto *bbuilder = static_cast<arrow::BooleanBuilder *>(builder);
      int64_t prev_bytes = (bbuilder->length() + 7) / 8;
      s = bbuilder->Append((bool) v);
      serialized_len = ((bbuilder->length() + 7) / 8) - prev_bytes;
      break;
    }
    case arrow::Type::TIMESTAMP:
    {
      gint64 v;
      if (!type_cast_to_int64(str, len, &v, NULL))
        return -1;
      s = static_cast<arrow::TimestampBuilder *>(builder)->Append((int64_t) v);
      serialized_len = sizeof(int64_t);
      break;
    }
    case arrow::Type::MAP:
      return this->append_map_string_string(static_cast<arrow::MapBuilder *>(builder), str, len);
    default:
      return -1;
    }

  return s.ok() ? serialized_len : -1;
}

LogThreadedResult
DestinationWorker::insert(LogMessage *msg)
{
  DestinationDriver *owner = this->get_owner();
  const auto &schema_fields = owner->get_schema_fields();

  size_t row_bytes = 0;

  for (size_t i = 0; i < schema_fields.size(); i++)
    {
      const auto &field = schema_fields[i];
      LogTemplateEvalOptions opts = { &owner->get_template_options(), LTZ_SEND,
                                      this->super->super.seq_num, NULL, LM_VT_STRING
                                    };
      LogMessageValueType vtype;
      const gchar *val;
      gssize val_len;
      bool null_value = false;

      if (log_template_is_trivial(field.value))
        {
          val = log_template_get_trivial_value_and_type(field.value, msg, &val_len, &vtype);
          if (val_len < 0 || vtype == LM_VT_NULL)
            null_value = true;
        }
      else
        {
          log_template_format_value_and_type(field.value, msg, &opts, this->buffer, &vtype);
          val = this->buffer->str;
          val_len = (gssize) this->buffer->len;
          null_value = (vtype == LM_VT_NULL);
        }

      gssize serialized_len = -1;
      if (!null_value)
        serialized_len = this->append_value(this->builders[i].get(), field.type, val, val_len);

      if (serialized_len < 0)
        {
          if (!null_value && !(owner->get_template_options().on_error & ON_ERROR_SILENT))
            msg_error("arrow-flight: Failed to convert field value, using null",
                      evt_tag_str("field", field.name.c_str()),
                      log_pipe_location_tag(&this->super->super.owner->super.super.super));
          auto null_status = this->builders[i]->AppendNull();
          if (!null_status.ok())
            {
              /* AppendNull() failure would leave columns at different lengths and break RecordBatch::Make.
               * Force a reconnect so disconnect() clears the builders and we start fresh. */
              msg_error("arrow-flight: Failed to append null to Arrow builder",
                        evt_tag_str("field", field.name.c_str()),
                        evt_tag_str("error", null_status.ToString().c_str()),
                        log_pipe_location_tag(&this->super->super.owner->super.super.super));
              return LTR_NOT_CONNECTED;
            }
        }
      else
        {
          row_bytes += (size_t) serialized_len;
        }
    }

  if (this->get_batch_size() == 1)
    this->current_batch_path = this->format_path(msg);

  this->current_batch_bytes += row_bytes;
  log_threaded_dest_driver_insert_msg_length_stats(this->super->super.owner, row_bytes);

  if (this->should_initiate_flush())
    return log_threaded_dest_worker_flush(&this->super->super, LTF_FLUSH_NORMAL);

  return LTR_QUEUED;
}

std::string
DestinationWorker::format_path(LogMessage *msg)
{
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

  LogTemplateEvalOptions opts = {&this->get_owner()->get_template_options(), LTZ_SEND,
                                 this->super->super.seq_num, NULL, LM_VT_STRING
                                };
  log_template_format(path_template, msg, &opts, this->buffer);
  return this->buffer->str;
}

bool
DestinationWorker::open_stream(const gchar *path)
{
  (void) this->close_stream();

  DestinationDriver *owner = this->get_owner();

  arrow::flight::FlightCallOptions call_options;
  glong timeout = owner->get_timeout();
  if (timeout > 0)
    call_options.timeout = arrow::flight::TimeoutDuration{(double) timeout};

  auto descriptor = arrow::flight::FlightDescriptor::Path({path});
  auto put_result = this->client->DoPut(call_options, descriptor, owner->get_arrow_schema());
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

arrow::Status
DestinationWorker::close_stream()
{
  if (!this->writer)
    return arrow::Status::OK();

  /* DoneWriting() half-closes the write side; Close() finalizes the call and surfaces the server's
   * terminal status. Either may carry the reason the server tore the stream down, so return it to
   * the caller for classification instead of swallowing it here. */
  arrow::Status done_status = this->writer->DoneWriting();
  arrow::Status close_status = this->writer->Close();

  this->writer.reset();
  this->metadata_reader.reset();
  this->current_stream_path.clear();

  return !close_status.ok() ? close_status : done_status;
}

static LogThreadedResult
_map_arrow_status_to_log_threaded_result(const arrow::Status &status)
{
  if (status.ok())
    return LTR_SUCCESS;

  switch (status.code())
    {
    case arrow::StatusCode::IOError:
    case arrow::StatusCode::Cancelled:
    case arrow::StatusCode::OutOfMemory:
    case arrow::StatusCode::UnknownError:
      goto temporary_error;
    case arrow::StatusCode::Invalid:
    case arrow::StatusCode::TypeError:
    case arrow::StatusCode::KeyError:
    case arrow::StatusCode::NotImplemented:
    case arrow::StatusCode::CapacityError:
    case arrow::StatusCode::IndexError:
    case arrow::StatusCode::SerializationError:
    case arrow::StatusCode::AlreadyExists:
      goto permanent_error;
    default:
      goto temporary_error;
    }

temporary_error:
  msg_info("Arrow Flight server responded with a temporary error status code, retrying after time-reopen() seconds",
           evt_tag_int("error_code", int(status.code())),
           evt_tag_str("error_message", status.message().c_str()),
           evt_tag_str("error_details", status.detail() ? status.detail()->ToString().c_str() : ""));
  return LTR_NOT_CONNECTED;

permanent_error:
  msg_error("Arrow Flight server responded with a permanent error status code, dropping batch",
            evt_tag_int("error_code", int(status.code())),
            evt_tag_str("error_message", status.message().c_str()),
            evt_tag_str("error_details", status.detail() ? status.detail()->ToString().c_str() : ""));
  return LTR_DROP;
}

LogThreadedResult
DestinationWorker::flush(LogThreadedFlushMode mode)
{
  DestinationDriver *owner = this->get_owner();
  LogThreadedResult result = LTR_ERROR;
  std::vector<std::shared_ptr<arrow::Array>> arrays;
  std::shared_ptr<arrow::RecordBatch> batch;
  std::shared_ptr<arrow::Buffer> ack;
  arrow::Status status;
  int64_t batch_bytes = 0;
  const gchar *path = this->current_batch_path.c_str();

  if (this->get_batch_size() == 0)
    return LTR_SUCCESS;

  for (auto &builder : this->builders)
    {
      auto finish_result = builder->Finish();
      if (!finish_result.ok())
        {
          msg_error("arrow-flight: Failed to finalize Arrow array",
                    evt_tag_str("error", finish_result.status().ToString().c_str()),
                    log_pipe_location_tag(&this->super->super.owner->super.super.super));
          /* Finish() may leave the builders in an undefined state; force a reconnect
           * so disconnect() resets them via create_builders(). */
          result = LTR_NOT_CONNECTED;
          goto exit;
        }
      arrays.push_back(*finish_result);
    }

  batch = arrow::RecordBatch::Make(owner->get_arrow_schema(), this->get_batch_size(), arrays);

  if (!this->writer || this->current_stream_path != path)
    {
      if (!this->open_stream(path))
        {
          result = LTR_NOT_CONNECTED;
          goto exit;
        }
    }

  status = this->writer->WriteRecordBatch(*batch);
  if (!status.ok())
    {
      msg_error("arrow-flight: Failed to write record batch",
                evt_tag_str("error", status.ToString().c_str()),
                log_pipe_location_tag(&this->super->super.owner->super.super.super));
      result = _map_arrow_status_to_log_threaded_result(status);
      /* Stream is likely broken on the server side; drop it so the next flush opens a fresh one. */
      (void) this->close_stream();
      goto exit;
    }

  status = this->metadata_reader->ReadMetadata(&ack);
  if (!status.ok())
    {
      msg_error("arrow-flight: Failed to read ack",
                evt_tag_str("error", status.ToString().c_str()),
                log_pipe_location_tag(&this->super->super.owner->super.super.super));
      result = _map_arrow_status_to_log_threaded_result(status);
      (void) this->close_stream();
      goto exit;
    }

  if (!ack)
    {
      /*
       * The only way for the server to raise an error result is to raise it async,
       * effectively closing the stream and not send an ack at all.
       * However, the raised error can only be read by the client with its next API call.
       * Close() will suffice as we want to close the stream anyways.
       */
      status = this->close_stream();
      if (!status.ok())
        {
          msg_error("arrow-flight: Server error during flush",
                    evt_tag_str("error", status.ToString().c_str()),
                    log_pipe_location_tag(&this->super->super.owner->super.super.super));
          result = _map_arrow_status_to_log_threaded_result(status);
        }
      else
        {
          result = LTR_NOT_CONNECTED;
        }
      goto exit;
    }

  status = arrow::ipc::GetRecordBatchSize(*batch, &batch_bytes);
  if (status.ok())
    {
      log_threaded_dest_worker_written_bytes_add(&this->super->super, batch_bytes);
      log_threaded_dest_driver_insert_batch_length_stats(this->super->super.owner, batch_bytes);
    }

  msg_debug("arrow-flight: Batch delivered",
            log_pipe_location_tag(&this->super->super.owner->super.super.super));
  result = LTR_SUCCESS;

exit:
  this->prepare_batch();
  return result;
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
