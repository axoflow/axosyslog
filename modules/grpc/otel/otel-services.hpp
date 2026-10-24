/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Attila Szakacs-Bertok <attila.szakacs@axoflow.com>
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

#ifndef OTEL_SERVICES_HPP
#define OTEL_SERVICES_HPP

#include <grpcpp/grpcpp.h>
#include <grpcpp/impl/client_unary_call.h>
#include <grpcpp/impl/codegen/proto_utils.h>
#include <grpcpp/impl/rpc_method.h>
#include <grpcpp/impl/rpc_service_method.h>
#include <grpcpp/impl/service_type.h>
#include <grpcpp/support/async_unary_call.h>

#include <memory>

namespace syslogng {
namespace grpc {
namespace otel {

constexpr const char *LOGS_EXPORT_METHOD = "/opentelemetry.proto.collector.logs.v1.LogsService/Export";
constexpr const char *METRICS_EXPORT_METHOD = "/opentelemetry.proto.collector.metrics.v1.MetricsService/Export";
constexpr const char *TRACE_EXPORT_METHOD = "/opentelemetry.proto.collector.trace.v1.TraceService/Export";

template <class Req, class Resp>
class AsyncExportService : public ::grpc::Service
{
public:
  AsyncExportService(const char *method_path)
  {
    this->AddMethod(new ::grpc::internal::RpcServiceMethod(method_path, ::grpc::internal::RpcMethod::NORMAL_RPC,
                                                           nullptr));
    this->MarkMethodAsync(0);
  }

  void RequestExport(::grpc::ServerContext *context, Req *request, ::grpc::ServerAsyncResponseWriter<Resp> *response,
                     ::grpc::CompletionQueue *new_call_cq, ::grpc::ServerCompletionQueue *notification_cq, void *tag)
  {
    this->RequestAsyncUnary(0, context, request, response, new_call_cq, notification_cq, tag);
  }
};

template <class Req, class Resp>
class ExportStub
{
public:
  ExportStub(const std::shared_ptr<::grpc::ChannelInterface> &channel_, const char *method_path)
    : channel(channel_), export_method(method_path, ::grpc::internal::RpcMethod::NORMAL_RPC, channel_)
  {
  }

  ::grpc::Status Export(::grpc::ClientContext *context, const Req &request, Resp *response)
  {
    return ::grpc::internal::BlockingUnaryCall(this->channel.get(), this->export_method, context, request, response);
  }

private:
  std::shared_ptr<::grpc::ChannelInterface> channel;
  const ::grpc::internal::RpcMethod export_method;
};

}
}
}

#endif
