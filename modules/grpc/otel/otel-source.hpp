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

#ifndef OTEL_SOURCE_HPP
#define OTEL_SOURCE_HPP

#include "otel-source.h"

#include "grpc-source.hpp"
#include "grpc-source-worker.hpp"
#include "otel-servicecall.hpp"

#include "compat/cpp-start.h"

void otel_sd_reload_save(LogThreadedSourceDriver *s, LogThreadedSourceWorker **workers, gint num_workers);
LogThreadedSourceWorker **otel_sd_reload_restore(LogThreadedSourceDriver *s, gint *num_workers);

#include "compat/cpp-end.h"

#include <grpcpp/grpcpp.h>
#include <grpcpp/alarm.h>
#include <memory>

namespace syslogng {
namespace grpc {
namespace otel {

class SourceDriver : public syslogng::grpc::SourceDriver
{
public:
  SourceDriver(GrpcSourceDriver *s);
  ~SourceDriver() override;

  bool init() override;
  void format_stats_key(StatsClusterKeyBuilder *kb) override;
  const char *generate_persist_name() override;
  LogThreadedSourceWorker *construct_worker(int worker_index) override;

  /* services must exist for the lifetime of the server instance */
  std::unique_ptr<TraceService::AsyncService> trace_service;
  std::unique_ptr<LogsService::AsyncService> logs_service;
  std::unique_ptr<MetricsService::AsyncService> metrics_service;

private:
  friend class SourceWorker;
  friend void ::otel_sd_reload_save(LogThreadedSourceDriver *s, LogThreadedSourceWorker **workers, gint num_workers);
  friend LogThreadedSourceWorker **::otel_sd_reload_restore(LogThreadedSourceDriver *s, gint *num_workers);

  void reload_save(LogThreadedSourceWorker **workers, gint num_workers);
  LogThreadedSourceWorker **reload_restore(gint *num_workers);
  void shutdown_server();

private:
  std::unique_ptr<::grpc::Server> server;
  std::list<std::unique_ptr<::grpc::ServerCompletionQueue>> cqs;
};

class SourceWorker : public syslogng::grpc::SourceWorker
{
public:
  SourceWorker(GrpcSourceWorker *s, std::unique_ptr<::grpc::ServerCompletionQueue> queue);
  ~SourceWorker() override;

  bool init() override;
  void run() override;
  void request_exit() override;

private:
  friend TraceServiceCall;
  friend LogsServiceCall;
  friend MetricsServiceCall;
  friend StopEventCall;

private:
  std::unique_ptr<::grpc::ServerCompletionQueue> cq;
  bool service_calls_registered;
  ::grpc::Alarm stop_scheduler;
  StopEventCall stop_call;
};

}
}
}

syslogng::grpc::otel::SourceDriver *otel_sd_get_cpp(GrpcSourceDriver *self);

#endif
