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

#include "opentelemetry/proto/collector/trace/v1/trace_service.pb.h"
#include "opentelemetry/proto/collector/logs/v1/logs_service.pb.h"
#include "opentelemetry/proto/collector/metrics/v1/metrics_service.pb.h"

#include "otel-services.hpp"

namespace syslogng {
namespace grpc {
namespace otel {

using opentelemetry::proto::collector::trace::v1::ExportTraceServiceRequest;
using opentelemetry::proto::collector::trace::v1::ExportTraceServiceResponse;
using opentelemetry::proto::collector::logs::v1::ExportLogsServiceRequest;
using opentelemetry::proto::collector::logs::v1::ExportLogsServiceResponse;
using opentelemetry::proto::collector::metrics::v1::ExportMetricsServiceRequest;
using opentelemetry::proto::collector::metrics::v1::ExportMetricsServiceResponse;

using TraceService = AsyncExportService<ExportTraceServiceRequest, ExportTraceServiceResponse>;
using LogsService = AsyncExportService<ExportLogsServiceRequest, ExportLogsServiceResponse>;
using MetricsService = AsyncExportService<ExportMetricsServiceRequest, ExportMetricsServiceResponse>;

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

using TraceServiceCall = AsyncServiceCall<TraceService,
      ExportTraceServiceRequest, ExportTraceServiceResponse>;
using LogsServiceCall = AsyncServiceCall<LogsService,
      ExportLogsServiceRequest, ExportLogsServiceResponse>;
using MetricsServiceCall = AsyncServiceCall<MetricsService,
      ExportMetricsServiceRequest, ExportMetricsServiceResponse>;

class StopEventCall;

}
}
}

#endif
