/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2023-2024 Attila Szakacs <attila.szakacs@axoflow.com>
 * Copyright (c) 2023 László Várady
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

#include "grpc-dest.hpp"
#include "grpc-dest-worker.hpp"

using namespace syslogng::grpc;

static const std::map<GrpcDestResponse, ::grpc::StatusCode> GRD_TO_GRPC_STATUSCODE_MAPPING =
{
  { GDR_OK, ::grpc::StatusCode::OK },
  { GDR_UNAVAILABLE, ::grpc::StatusCode::UNAVAILABLE },
  { GDR_CANCELLED, ::grpc::StatusCode::CANCELLED },
  { GDR_DEADLINE_EXCEEDED, ::grpc::StatusCode::DEADLINE_EXCEEDED },
  { GDR_ABORTED, ::grpc::StatusCode::ABORTED },
  { GDR_OUT_OF_RANGE, ::grpc::StatusCode::OUT_OF_RANGE },
  { GDR_DATA_LOSS, ::grpc::StatusCode::DATA_LOSS },
  { GDR_UNKNOWN, ::grpc::StatusCode::UNKNOWN },
  { GDR_INVALID_ARGUMENT, ::grpc::StatusCode::INVALID_ARGUMENT },
  { GDR_NOT_FOUND, ::grpc::StatusCode::NOT_FOUND },
  { GDR_ALREADY_EXISTS, ::grpc::StatusCode::ALREADY_EXISTS },
  { GDR_PERMISSION_DENIED, ::grpc::StatusCode::PERMISSION_DENIED },
  { GDR_UNAUTHENTICATED, ::grpc::StatusCode::UNAUTHENTICATED },
  { GDR_FAILED_PRECONDITION, ::grpc::StatusCode::FAILED_PRECONDITION },
  { GDR_UNIMPLEMENTED, ::grpc::StatusCode::UNIMPLEMENTED },
  { GDR_INTERNAL, ::grpc::StatusCode::INTERNAL },
  { GDR_RESOURCE_EXHAUSTED, ::grpc::StatusCode::RESOURCE_EXHAUSTED },
};

/* C++ Implementations */

DestDriver::DestDriver(GrpcDestDriver *s)
  : super(s), compression(false), batch_bytes(4 * 1000 * 1000),
    keepalive_time(-1), keepalive_timeout(-1), keepalive_max_pings_without_data(-1),
    flush_on_key_change(false), dynamic_headers_enabled(false),
    response_actions({ GDRA_UNSET })
{
  log_template_options_defaults(&this->template_options);
  credentials_builder_wrapper.self = &credentials_builder;
}

DestDriver::~DestDriver()
{
  log_template_unref(this->proto_var);
  log_template_options_destroy(&this->template_options);
}

bool
DestDriver::set_worker_partition_key()
{
  GlobalConfig *cfg = log_pipe_get_config(&this->super->super.super.super.super);

  LogTemplate *worker_partition_key_tpl = log_template_new(cfg, NULL);
  if (!log_template_compile(worker_partition_key_tpl, this->worker_partition_key.str().c_str(), NULL))
    {
      msg_error("Error compiling worker partition key template",
                evt_tag_str("template", this->worker_partition_key.str().c_str()));
      return false;
    }

  if (log_template_is_literal_string(worker_partition_key_tpl))
    {
      log_template_unref(worker_partition_key_tpl);
    }
  else
    {
      log_threaded_dest_driver_set_worker_partition_key_ref(&this->super->super.super.super, worker_partition_key_tpl);
      log_threaded_dest_driver_set_flush_on_worker_key_change(&this->super->super.super.super,
                                                              this->flush_on_key_change);
    }

  return true;
}

bool
DestDriver::init()
{
  GlobalConfig *cfg = log_pipe_get_config(&this->super->super.super.super.super);

  if (url.length() == 0)
    {
      msg_error("url() option is mandatory",
                log_pipe_location_tag(&super->super.super.super.super));
      return false;
    }

  if (!credentials_builder.validate())
    {
      return false;
    }

  if (this->worker_partition_key.rdbuf()->in_avail() && !this->set_worker_partition_key())
    return false;

  log_template_options_init(&this->template_options, cfg);

  if (!log_threaded_dest_driver_init_method(&this->super->super.super.super.super))
    return false;

  if (this->batch_bytes > 0 && this->super->super.batch_lines <= 0)
    this->super->super.batch_lines = G_MAXINT;

  StatsClusterKeyBuilder *kb = stats_cluster_key_builder_new();
  format_stats_key(kb);
  metrics.init(kb, log_pipe_is_internal(&super->super.super.super.super) ? STATS_LEVEL3 : STATS_LEVEL1);

  return true;
}

bool
DestDriver::deinit()
{
  metrics.deinit();
  return log_threaded_dest_driver_deinit_method(&super->super.super.super.super);
}

bool
DestDriver::handle_response(const ::grpc::Status &status, LogThreadedResult *ltr)
{
  if (status.error_code() >= GRPC_DEST_RESPONSE_ACTIONS_ARRAY_LEN)
    {
      msg_error("Invalid gRPC status code", evt_tag_int("status_code", status.error_code()));
      return false;
    }

  const char *action_as_string;
  bool debug_log = false;

  GrpcDestResponseAction action = this->response_actions[status.error_code()];
  switch (action)
    {
    case GDRA_UNSET:
      return false;

    case GDRA_DISCONNECT:
      action_as_string = "disconnect";
      *ltr = LTR_NOT_CONNECTED;
      break;

    case GDRA_DROP:
      action_as_string = "drop";
      *ltr = LTR_DROP;
      break;

    case GDRA_RETRY:
      action_as_string = "retry";
      *ltr = LTR_ERROR;
      break;

    case GDRA_SUCCESS:
      action_as_string = "success";
      debug_log = true;
      *ltr = LTR_SUCCESS;
      break;

    default:
      g_assert_not_reached();
    }

  if (debug_log)
    {
      msg_debug("gRPC: handled by response-action()",
                evt_tag_str("action", action_as_string),
                evt_tag_str("url", this->url.c_str()),
                evt_tag_int("error_code", status.error_code()),
                evt_tag_str("error_message", status.error_message().c_str()),
                evt_tag_str("error_details", status.error_details().c_str()),
                evt_tag_str("driver", this->super->super.super.super.id),
                log_pipe_location_tag(&this->super->super.super.super.super));
    }
  else
    {
      msg_notice("gRPC: handled by response-action()",
                 evt_tag_str("action", action_as_string),
                 evt_tag_str("url", this->url.c_str()),
                 evt_tag_int("error_code", status.error_code()),
                 evt_tag_str("error_message", status.error_message().c_str()),
                 evt_tag_str("error_details", status.error_details().c_str()),
                 evt_tag_str("driver", this->super->super.super.super.id),
                 log_pipe_location_tag(&this->super->super.super.super.super));
    }

  return true;
}

/* C Wrappers */

void
grpc_dd_set_url(LogDriver *s, const gchar *url)
{
  GrpcDestDriver *self = (GrpcDestDriver *) s;
  self->cpp->set_url(url);
}

void
grpc_dd_set_compression(LogDriver *s, gboolean enable)
{
  GrpcDestDriver *self = (GrpcDestDriver *) s;
  self->cpp->set_compression(enable);
}

void
grpc_dd_set_batch_bytes(LogDriver *s, glong b)
{
  GrpcDestDriver *self = (GrpcDestDriver *) s;
  self->cpp->set_batch_bytes((size_t) b);
}

void
grpc_dd_set_keepalive_time(LogDriver *s, gint t)
{
  GrpcDestDriver *self = (GrpcDestDriver *) s;
  self->cpp->set_keepalive_time(t);
}

void
grpc_dd_set_keepalive_timeout(LogDriver *s, gint t)
{
  GrpcDestDriver *self = (GrpcDestDriver *) s;
  self->cpp->set_keepalive_timeout(t);
}

void
grpc_dd_set_keepalive_max_pings(LogDriver *s, gint p)
{
  GrpcDestDriver *self = (GrpcDestDriver *) s;
  self->cpp->set_keepalive_max_pings(p);
}

void
grpc_dd_add_int_channel_arg(LogDriver *s, const gchar *name, glong value)
{
  GrpcDestDriver *self = (GrpcDestDriver *) s;
  self->cpp->add_extra_channel_arg(name, value);
}

void
grpc_dd_add_string_channel_arg(LogDriver *s, const gchar *name, const gchar *value)
{
  GrpcDestDriver *self = (GrpcDestDriver *) s;
  self->cpp->add_extra_channel_arg(name, value);
}

gboolean
grpc_dd_add_header(LogDriver *s, const gchar *name, LogTemplate *value)
{
  GrpcDestDriver *self = (GrpcDestDriver *) s;
  return self->cpp->add_header(name, value);
}

gboolean
grpc_dd_add_schema_field(LogDriver *d, const gchar *name, const gchar *type, LogTemplate *value)
{
  GrpcDestDriver *self = (GrpcDestDriver *) d;
  LogMessageProtobufFormatter *schema = self->cpp->get_log_message_protobuf_formatter();
  g_assert(schema);
  return schema->add_field(name, type ? type : "", value);
}

void
grpc_dd_set_protobuf_schema(LogDriver *d, const gchar *proto_path, GList *values)
{
  GrpcDestDriver *self = (GrpcDestDriver *) d;
  LogMessageProtobufFormatter *schema = self->cpp->get_log_message_protobuf_formatter();
  g_assert(schema);
  schema->set_protobuf_schema(proto_path, values);
}

void
grpc_dd_set_proto_var(LogDriver *d, LogTemplate *proto_var)
{
  GrpcDestDriver *self = (GrpcDestDriver *) d;
  self->cpp->set_proto_var(proto_var);
}

void
grpc_dd_set_response_action(LogDriver *d, GrpcDestResponse response, GrpcDestResponseAction action)
{
  GrpcDestDriver *self = (GrpcDestDriver *) d;
  self->cpp->set_response_action(GRD_TO_GRPC_STATUSCODE_MAPPING.at(response), action);
}

LogTemplateOptions *
grpc_dd_get_template_options(LogDriver *d)
{
  GrpcDestDriver *self = (GrpcDestDriver *) d;
  return &self->cpp->get_template_options();
}

GrpcClientCredentialsBuilderW *
grpc_dd_get_credentials_builder(LogDriver *s)
{
  GrpcDestDriver *self = (GrpcDestDriver *) s;
  return self->cpp->get_credentials_builder_wrapper();
}

static const gchar *
_format_stats_key(LogThreadedDestDriver *s, StatsClusterKeyBuilder *kb)
{
  GrpcDestDriver *self = (GrpcDestDriver *) s;
  return self->cpp->format_stats_key(kb);
}

static const gchar *
_generate_persist_name(const LogPipe *s)
{
  GrpcDestDriver *self = (GrpcDestDriver *) s;
  return self->cpp->generate_persist_name();
}

static gboolean
_init(LogPipe *s)
{
  GrpcDestDriver *self = (GrpcDestDriver *) s;
  return self->cpp->init();
}

static gboolean
_deinit(LogPipe *s)
{
  GrpcDestDriver *self = (GrpcDestDriver *) s;
  return self->cpp->deinit();
}

static LogThreadedDestWorker *
_construct_worker(LogThreadedDestDriver *s, gint worker_index)
{
  GrpcDestDriver *self = (GrpcDestDriver *) s;
  return self->cpp->construct_worker(worker_index);
}

static void
_free(LogPipe *s)
{
  GrpcDestDriver *self = (GrpcDestDriver *) s;
  delete self->cpp;
  log_threaded_dest_driver_free(s);
}

GrpcDestDriver *
grpc_dd_new(GlobalConfig *cfg, const gchar *stats_name)
{
  GrpcDestDriver *self = g_new0(GrpcDestDriver, 1);

  log_threaded_dest_driver_init_instance(&self->super, cfg);

  self->super.super.super.super.init = _init;
  self->super.super.super.super.deinit = _deinit;
  self->super.super.super.super.free_fn = _free;
  self->super.super.super.super.generate_persist_name = _generate_persist_name;

  self->super.worker.construct = _construct_worker;
  self->super.stats_source = stats_register_type(stats_name);
  self->super.format_stats_key = _format_stats_key;

  return self;
}
