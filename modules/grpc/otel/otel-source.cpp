/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2023-2024 Attila Szakacs <attila.szakacs@axoflow.com>
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

#include "otel-source.hpp"
#include "otel-source-services.hpp"

#include "compat/cpp-start.h"
#include "messages.h"
#include "cfg.h"
#include "compat/cpp-end.h"

#include <string>

#include <grpcpp/grpcpp.h>

using namespace syslogng::grpc::otel;

namespace {

struct CfgReloadEntry
{
  LogThreadedSourceWorker **workers;
  int num_workers;
  std::unique_ptr<::grpc::Server> server;
  std::unique_ptr<TraceService::AsyncService> trace_service;
  std::unique_ptr<LogsService::AsyncService> logs_service;
  std::unique_ptr<MetricsService::AsyncService> metrics_service;
};

}

#include "compat/cpp-start.h"

static void
_destroy_reload_entry(void *c)
{
  CfgReloadEntry *e = static_cast<CfgReloadEntry *>(c);

  e->server->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds(30));
  log_threaded_source_driver_destroy_workers(e->workers, e->num_workers);

  delete e;
}

#include "compat/cpp-end.h"

SourceDriver::SourceDriver(GrpcSourceDriver *s)
  : syslogng::grpc::SourceDriver(s)
{
  this->port = 4317;
  this->trace_service = std::make_unique<TraceService::AsyncService>();
  this->logs_service = std::make_unique<LogsService::AsyncService>();
  this->metrics_service = std::make_unique<MetricsService::AsyncService>();
}

void
SourceDriver::shutdown_server()
{
  if (this->server)
    {
      msg_debug("Shutting down OpenTelemetry server", evt_tag_int("port", this->port));
      this->server->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds(30));
    }
}

void
SourceDriver::format_stats_key(StatsClusterKeyBuilder *kb)
{
  stats_cluster_key_builder_add_legacy_label(kb, stats_cluster_label("driver", "opentelemetry"));

  gchar num[64];
  g_snprintf(num, sizeof(num), "%" G_GUINT32_FORMAT, this->port);
  stats_cluster_key_builder_add_legacy_label(kb, stats_cluster_label("port", num));
}

const char *
SourceDriver::generate_persist_name()
{
  // TODO: channel args, port, etc.   scaling unsupported -> workers
  static char persist_name[1024];

  if (this->super->super.super.super.super.persist_name)
    g_snprintf(persist_name, sizeof(persist_name), "opentelemetry.%s",
               this->super->super.super.super.super.persist_name);
  else
    g_snprintf(persist_name, sizeof(persist_name), "opentelemetry(%" G_GUINT32_FORMAT ")",
               this->port);

  return persist_name;
}

bool
SourceDriver::init()
{
  this->super->super.worker_options.super.keep_hostname = TRUE;

  ::grpc::ServerBuilder builder;
  if (!this->prepare_server_builder(builder))
    return false;

  builder.RegisterService(this->trace_service.get());
  builder.RegisterService(this->logs_service.get());
  builder.RegisterService(this->metrics_service.get());

  for (int i = 0; i < this->super->super.num_workers; i++)
    this->cqs.push_back(builder.AddCompletionQueue());

  bool workers_inited = syslogng::grpc::SourceDriver::init();

  this->cqs.clear();

  if (!workers_inited)
    return false;

  if (this->server)
    goto exit;

  this->server = builder.BuildAndStart();
  if (!this->server)
    {
      msg_error("Failed to start OpenTelemetry server", evt_tag_int("port", this->port));
      return false;
    }

exit:
  msg_info("OpenTelemetry server accepting connections", evt_tag_int("port", this->port));
  return true;
}

void
SourceDriver::reload_save(LogThreadedSourceWorker **workers, gint num_workers)
{
  GlobalConfig *cfg = log_pipe_get_config(&this->super->super.super.super.super);

  CfgReloadEntry *reload_entry = new CfgReloadEntry
  {
    workers,
    num_workers,
    std::move(this->server),
    std::move(this->trace_service),
    std::move(this->logs_service),
    std::move(this->metrics_service),
  };
  cfg_persist_config_add(cfg, this->generate_persist_name(), reload_entry, _destroy_reload_entry);
}

LogThreadedSourceWorker **
SourceDriver::reload_restore(gint *num_workers)
{
  GlobalConfig *cfg = log_pipe_get_config(&this->super->super.super.super.super);

  CfgReloadEntry *reload_entry = static_cast<CfgReloadEntry *>(
                                   cfg_persist_config_fetch(cfg, this->generate_persist_name()));

  if (!reload_entry)
    return NULL;

  this->trace_service = std::move(reload_entry->trace_service),
        this->logs_service = std::move(reload_entry->logs_service),
              this->metrics_service = std::move(reload_entry->metrics_service),
                    this->server = std::move(reload_entry->server);
  LogThreadedSourceWorker **workers = reload_entry->workers;
  *num_workers = reload_entry->num_workers;

  delete reload_entry;

  return workers;
}

SourceDriver::~SourceDriver()
{
  this->shutdown_server();
}

LogThreadedSourceWorker *
SourceDriver::construct_worker(int worker_index)
{
  GrpcSourceWorker *worker = grpc_sw_new(this->super, worker_index);
  worker->cpp = new SourceWorker(worker, std::move(this->cqs.front()));
  this->cqs.pop_front();
  return &worker->super;
}


SourceWorker::SourceWorker(GrpcSourceWorker *s, std::unique_ptr<::grpc::ServerCompletionQueue> queue)
  : syslogng::grpc::SourceWorker(s), cq(std::move(queue)), service_calls_registered(false)
{
}

SourceWorker::~SourceWorker()
{
  SourceDriver &owner = static_cast<SourceDriver &>(this->get_owner());
  owner.shutdown_server();

  if (!this->cq)
    return;

  this->cq->Shutdown();

  void *tag;
  bool ignored_ok;
  while (this->cq->Next(&tag, &ignored_ok))
    {
      auto call = static_cast<AsyncServiceCallInterface *>(tag);
      delete call;
    }
}

bool
syslogng::grpc::otel::SourceWorker::init()
{
  if (this->service_calls_registered)
    return true;

  SourceDriver &owner = static_cast<SourceDriver &>(this->get_owner());

  /* Proceed() will immediately create a new ServiceCall,
   * so creating 1 ServiceCall here results in 2 concurrent requests.
   *
   * Because of this we should create (concurrent_requests - 1) ServiceCalls here.
   */
  for (int i = 0; i < owner.concurrent_requests - 1; i++)
    {
      new TraceServiceCall(*this, owner.trace_service.get(), this->cq.get());
      new LogsServiceCall(*this, owner.logs_service.get(), this->cq.get());
      new MetricsServiceCall(*this, owner.metrics_service.get(), this->cq.get());
    }

  this->service_calls_registered = true;

  return true;
}

void
syslogng::grpc::otel::SourceWorker::run()
{
  void *tag;
  bool ok;
  while (this->cq->Next(&tag, &ok))
    {
      auto call = static_cast<AsyncServiceCallInterface *>(tag);

      if (typeid(*call) == typeid(StopEventCall))
        break;

      /* call may be freed after calling Proceed() y*/
      call->Proceed(ok);
    }
}

void
syslogng::grpc::otel::SourceWorker::request_exit()
{
  this->stop_scheduler.Set(this->cq.get(), gpr_inf_past(GPR_CLOCK_MONOTONIC), &this->stop_call);
}

SourceDriver *
otel_sd_get_cpp(GrpcSourceDriver *self)
{
  return (SourceDriver *) self->cpp;
}

void
otel_sd_reload_save(LogThreadedSourceDriver *s, LogThreadedSourceWorker **workers, gint num_workers)
{
  SourceDriver *self = otel_sd_get_cpp((GrpcSourceDriver *) s);
  self->reload_save(workers, num_workers);
}

LogThreadedSourceWorker **
otel_sd_reload_restore(LogThreadedSourceDriver *s, gint *num_workers)
{
  SourceDriver *self = otel_sd_get_cpp((GrpcSourceDriver *) s);
  return self->reload_restore(num_workers);
}

LogDriver *
otel_sd_new(GlobalConfig *cfg)
{
  GrpcSourceDriver *self = grpc_sd_new(cfg, "opentelemetry", "otlp");

  self->super.reload_keep_alive = TRUE;
  self->super.reload_save = otel_sd_reload_save;
  self->super.reload_restore = otel_sd_reload_restore;

  self->cpp = new SourceDriver(self);
  return &self->super.super.super;
}
