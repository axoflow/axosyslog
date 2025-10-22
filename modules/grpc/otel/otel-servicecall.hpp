/*
 * Copyright (c) 2023 Attila Szakacs
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

#ifndef OTEL_SERVICECALL_HPP
#define OTEL_SERVICECALL_HPP

#include "opentelemetry/proto/collector/trace/v1/trace_service.grpc.pb.h"
#include "opentelemetry/proto/collector/logs/v1/logs_service.grpc.pb.h"
#include "opentelemetry/proto/collector/metrics/v1/metrics_service.grpc.pb.h"

namespace syslogng {
namespace grpc {
namespace otel {

using opentelemetry::proto::collector::trace::v1::TraceService;
using opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest;
using opentelemetry::proto::collector::trace::v1::ExportTraceServiceResponse;
using opentelemetry::proto::collector::logs::v1::LogsService;
using opentelemetry::proto::collector::logs::v1::ExportLogsServiceRequest;
using opentelemetry::proto::collector::logs::v1::ExportLogsServiceResponse;
using opentelemetry::proto::collector::metrics::v1::MetricsService;
using opentelemetry::proto::collector::metrics::v1::ExportMetricsServiceRequest;
using opentelemetry::proto::collector::metrics::v1::ExportMetricsServiceResponse;

class AsyncServiceCallInterface
{
public:
  virtual void Proceed(bool ok) = 0;
  virtual ~AsyncServiceCallInterface() = default;
};

template <class S, class Req, class Res>
class AsyncServiceCall;

class StopEventCall final: public AsyncServiceCallInterface
{
public:
  void Proceed(bool ok) override {}
};

using TraceServiceCall = AsyncServiceCall<TraceService::AsyncService,
      ExportTraceServiceRequest, ExportTraceServiceResponse>;
using LogsServiceCall = AsyncServiceCall<LogsService::AsyncService,
      ExportLogsServiceRequest, ExportLogsServiceResponse>;
using MetricsServiceCall = AsyncServiceCall<MetricsService::AsyncService,
      ExportMetricsServiceRequest, ExportMetricsServiceResponse>;

class StopEventCall;

}
}
}

#endif
