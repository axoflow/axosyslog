/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 Attila Szakacs <attila.szakacs@axoflow.com>
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

#include "clickhouse-dest-worker.hpp"
#include "clickhouse-dest.hpp"

#include <google/protobuf/util/delimited_message_util.h>

using syslogng::grpc::clickhouse::DestWorker;
using syslogng::grpc::clickhouse::DestDriver;

bool
DestWorker::init()
{
  if (!syslogng::grpc::DestWorker::init())
    return false;
  this->stub = ::clickhouse::grpc::ClickHouse::NewStub(this->channel);
  return true;
}

void
DestWorker::deinit()
{
  this->stub.reset();
  syslogng::grpc::DestWorker::deinit();
}

bool
DestWorker::connect()
{
  if (!syslogng::grpc::DestWorker::connect())
    {
      msg_error("Error connecting to ClickHouse",
                evt_tag_str("url", this->owner.get_url().c_str()),
                log_pipe_location_tag(&this->super->super.owner->super.super.super));
      return false;
    }
  return true;
}

DestWorker::DestWorker(GrpcDestWorker *s)
  : syslogng::grpc::DestWorker(s)
{
}

bool
DestWorker::should_initiate_flush()
{
  return this->current_batch_bytes >= this->get_owner()->batch_bytes;
}

bool
DestWorker::insert_query_data_from_protovar(LogMessage *msg)
{
  DestDriver *owner_ = this->get_owner();
  ssize_t len;
  const gchar *serialized = owner_->format_proto_var(msg, &len);
  if (!serialized)
    return false;

  google::protobuf::io::OstreamOutputStream zero_copy_output(&this->query_data);
  google::protobuf::io::CodedOutputStream coded_output(&zero_copy_output);
  coded_output.WriteVarint32(len);
  coded_output.WriteRaw(serialized, len);
  return true;
}

bool
DestWorker::insert_query_data_from_jsonvar(LogMessage *msg)
{
  DestDriver *owner_ = this->get_owner();
  ssize_t len;
  const gchar *json_str = owner_->format_json_var(msg, &len);
  if (!json_str)
    return false;
  this->query_data.write(json_str, len);
  this->query_data.put('\n');
  return true;
}

bool
DestWorker::insert_query_data_from_schema(LogMessage *msg)
{
  DestDriver *owner_ = this->get_owner();
  google::protobuf::Message *message = nullptr;
  message = owner_->log_message_protobuf_formatter.format(msg, this->super->super.seq_num);
  if (!message)
    return false;
  bool success = google::protobuf::util::SerializeDelimitedToOstream(*message, &this->query_data);
  delete message;
  return success;
}

LogThreadedResult
DestWorker::insert(LogMessage *msg)
{
  DestDriver *owner_ = this->get_owner();
  std::streampos last_pos = this->query_data.tellp();
  size_t row_bytes = 0;

  if (owner_->proto_var)
    {
      if (!this->insert_query_data_from_protovar(msg))
        goto drop;
    }
  else if (owner_->json_mode())
    {
      if (!this->insert_query_data_from_jsonvar(msg))
        goto drop;
    }
  else
    {
      if (!this->insert_query_data_from_schema(msg))
        goto drop;
    }

  this->batch_size++;

  row_bytes = this->query_data.tellp() - last_pos;
  this->current_batch_bytes += row_bytes;
  log_threaded_dest_driver_insert_msg_length_stats(this->super->super.owner, row_bytes);

  msg_trace("Message added to ClickHouse batch", log_pipe_location_tag(&this->super->super.owner->super.super.super));

  if (!this->client_context.get())
    {
      this->client_context = std::make_unique<::grpc::ClientContext>();
      prepare_context_dynamic(*this->client_context, msg);
    }

  if (this->should_initiate_flush())
    return log_threaded_dest_worker_flush(&this->super->super, LTF_FLUSH_NORMAL);

  return LTR_QUEUED;

drop:
  if (!(owner_->template_options.on_error & ON_ERROR_SILENT))
    {
      msg_error("Failed to format message for ClickHouse, dropping message",
                log_pipe_location_tag(&this->super->super.owner->super.super.super));
    }

  /* LTR_DROP currently drops the entire batch */
  return LTR_QUEUED;
}

void
DestWorker::prepare_query_info(::clickhouse::grpc::QueryInfo &query_info)
{
  DestDriver *owner_ = this->get_owner();

  query_info.set_database(owner_->get_database());
  query_info.set_user_name(owner_->get_user());
  query_info.set_password(owner_->get_password());
  query_info.set_query(owner_->get_query());
  query_info.set_input_data(this->query_data.str());
}

static LogThreadedResult
_map_grpc_status_to_log_threaded_result(const ::grpc::Status &status)
{
  // TODO: this is based on OTLP, we should check how the ClickHouse gRPC server behaves

  switch (status.error_code())
    {
    case ::grpc::StatusCode::OK:
      return LTR_SUCCESS;
    case ::grpc::StatusCode::UNAVAILABLE:
    case ::grpc::StatusCode::CANCELLED:
    case ::grpc::StatusCode::DEADLINE_EXCEEDED:
    case ::grpc::StatusCode::ABORTED:
    case ::grpc::StatusCode::OUT_OF_RANGE:
    case ::grpc::StatusCode::DATA_LOSS:
      goto temporary_error;
    case ::grpc::StatusCode::UNKNOWN:
    case ::grpc::StatusCode::INVALID_ARGUMENT:
    case ::grpc::StatusCode::NOT_FOUND:
    case ::grpc::StatusCode::ALREADY_EXISTS:
    case ::grpc::StatusCode::PERMISSION_DENIED:
    case ::grpc::StatusCode::UNAUTHENTICATED:
    case ::grpc::StatusCode::FAILED_PRECONDITION:
    case ::grpc::StatusCode::UNIMPLEMENTED:
    case ::grpc::StatusCode::INTERNAL:
      goto permanent_error;
    case ::grpc::StatusCode::RESOURCE_EXHAUSTED:
      if (status.error_details().length() > 0)
        goto temporary_error;
      goto permanent_error;
    default:
      g_assert_not_reached();
    }

temporary_error:
  msg_info("ClickHouse server responded with a temporary error status code, retrying after time-reopen() seconds",
           evt_tag_int("error_code", status.error_code()),
           evt_tag_str("error_message", status.error_message().c_str()),
           evt_tag_str("error_details", status.error_details().c_str()));
  return LTR_NOT_CONNECTED;

permanent_error:
  msg_error("ClickHouse server responded with a permanent error status code, dropping batch",
            evt_tag_int("error_code", status.error_code()),
            evt_tag_str("error_message", status.error_message().c_str()),
            evt_tag_str("error_details", status.error_details().c_str()));
  return LTR_DROP;
}

void
DestWorker::prepare_batch()
{
  this->query_data.str("");
  this->query_data.clear();
  this->batch_size = 0;
  this->current_batch_bytes = 0;
  this->client_context.reset();
}

LogThreadedResult
DestWorker::flush(LogThreadedFlushMode mode)
{
  if (this->batch_size == 0)
    return LTR_SUCCESS;

  ::clickhouse::grpc::QueryInfo query_info;
  ::clickhouse::grpc::Result query_result;

  this->prepare_query_info(query_info);

  ::grpc::Status status = this->stub->ExecuteQuery(this->client_context.get(), query_info, &query_result);

  LogThreadedResult result;
  if (owner.handle_response(status, &result))
    {
      if (result == LTR_SUCCESS)
        goto success;
      goto error;
    }

  result = _map_grpc_status_to_log_threaded_result(status);
  if (result != LTR_SUCCESS)
    goto error;

  if (query_result.has_exception())
    {
      const ::clickhouse::grpc::Exception &exception = query_result.exception();
      msg_error("ClickHouse server responded with an exception, dropping batch",
                evt_tag_int("code", exception.code()),
                evt_tag_str("name", exception.name().c_str()),
                evt_tag_str("display_text", exception.display_text().c_str()),
                evt_tag_str("stack_trace", exception.stack_trace().c_str()));
      result = LTR_DROP;
      goto error;
    }

success:
  log_threaded_dest_worker_written_bytes_add(&this->super->super, this->current_batch_bytes);
  log_threaded_dest_driver_insert_batch_length_stats(this->super->super.owner, this->current_batch_bytes);

  msg_debug("ClickHouse batch delivered", log_pipe_location_tag(&this->super->super.owner->super.super.super));

error:
  this->get_owner()->metrics.insert_grpc_request_stats(status);
  this->prepare_batch();
  return result;
}

DestDriver *
DestWorker::get_owner()
{
  return clickhouse_dd_get_cpp(this->owner.super);
}
