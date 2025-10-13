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
#include "compat/cpp-end.h"

#include <string>

#include <grpcpp/grpcpp.h>

using namespace syslogng::grpc::otel;

SourceDriver::SourceDriver(GrpcSourceDriver *s)
  : syslogng::grpc::SourceDriver(s)
{
  this->port = 4317;
}

void
SourceDriver::shutdown()
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
  static char persist_name[1024];

  if (this->super->super.super.super.super.persist_name)
    g_snprintf(persist_name, sizeof(persist_name), "opentelemetry.%s",
               this->super->super.super.super.super.persist_name);
  else
    g_snprintf(persist_name, sizeof(persist_name), "opentelemetry(%" G_GUINT32_FORMAT ")",
               this->port);

  return persist_name;
}

gboolean
SourceDriver::init()
{
  this->super->super.worker_options.super.keep_hostname = TRUE;

  ::grpc::ServerBuilder builder;
  if (!this->prepare_server_builder(builder))
    return FALSE;

  trace_service = std::make_unique<TraceService::AsyncService>();
  logs_service = std::make_unique<LogsService::AsyncService>();
  metrics_service = std::make_unique<MetricsService::AsyncService>();

  builder.RegisterService(this->trace_service.get());
  builder.RegisterService(this->logs_service.get());
  builder.RegisterService(this->metrics_service.get());

  for (int i = 0; i < this->super->super.num_workers; i++)
    this->cqs.push_back(builder.AddCompletionQueue());

  this->server = builder.BuildAndStart();
  if (!this->server)
    {
      msg_error("Failed to start OpenTelemetry server", evt_tag_int("port", this->port));
      return FALSE;
    }

  if (!syslogng::grpc::SourceDriver::init())
    return FALSE;

  msg_info("OpenTelemetry server accepting connections", evt_tag_int("port", this->port));
  return TRUE;
}

void
SourceDriver::reload_save(LogThreadedSourceWorker **workers, gint num_workers)
{

}

LogThreadedSourceWorker **
SourceDriver::reload_restore(gint *num_workers)
{

}

SourceDriver::~SourceDriver()
{
  this->shutdown();
  this->drain_unused_queues();

  /* the service must exist for the lifetime of the Server instance */
  this->trace_service = nullptr;
  this->logs_service = nullptr;
  this->metrics_service = nullptr;
}

LogThreadedSourceWorker *
SourceDriver::construct_worker(int worker_index)
{
  GrpcSourceWorker *worker = grpc_sw_new(this->super, worker_index);
  worker->cpp = new SourceWorker(worker);
  return &worker->super;
}


SourceWorker::SourceWorker(GrpcSourceWorker *s)
  : syslogng::grpc::SourceWorker(s)
{
  SourceDriver &owner = static_cast<SourceDriver &>(this->get_owner());
  this->cq = std::move(owner.cqs.front());
  owner.cqs.pop_front();
}

SourceWorker::~SourceWorker()
{
  this->shutdown();
  this->drain_queue();
}

void
syslogng::grpc::otel::SourceWorker::run()
{
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

  void *tag;
  bool ok;
  while (this->cq->Next(&tag, &ok))
    {
      static_cast<AsyncServiceCallInterface *>(tag)->Proceed(ok);
    }

  this->cq.reset();
}

void
syslogng::grpc::otel::SourceWorker::request_exit()
{
  this->shutdown();
}

void
syslogng::grpc::otel::SourceWorker::shutdown()
{
  SourceDriver *owner_ = otel_sd_get_cpp(this->driver.super);

  owner_->shutdown();
  if (this->cq)
    this->cq->Shutdown();
}

void
syslogng::grpc::otel::SourceWorker::drain_queue()
{
  if (!this->cq)
    return;

  void *ignored_tag;
  bool ignored_ok;
  while (this->cq->Next(&ignored_tag, &ignored_ok)) {}
}

void syslogng::grpc::otel::SourceDriver::drain_unused_queues()
{
  for (auto &cq : this->cqs)
    {
      void *ignored_tag;
      bool ignored_ok;
      while (cq->Next(&ignored_tag, &ignored_ok)) {}
    }
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

  self->super.reload_save = otel_sd_reload_save;
  self->super.reload_restore = otel_sd_reload_restore;

  self->cpp = new SourceDriver(self);
  return &self->super.super.super;
}
