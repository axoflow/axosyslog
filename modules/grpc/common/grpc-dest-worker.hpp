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

#ifndef GRPC_DEST_WORKER_HPP
#define GRPC_DEST_WORKER_HPP

#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

#include "grpc-dest.hpp"

typedef struct GrpcDestWorker_ GrpcDestWorker;

namespace syslogng {
namespace grpc {

class DestWorker
{
public:
  DestWorker(GrpcDestWorker *s);
  virtual ~DestWorker() {};

  virtual bool init();
  virtual void deinit();
  virtual LogThreadedResult insert(LogMessage *msg) = 0;
  virtual LogThreadedResult flush(LogThreadedFlushMode mode) = 0;
  virtual bool connect();
  virtual void disconnect();

protected:
  void prepare_context(::grpc::ClientContext &context);
  void prepare_context_dynamic(::grpc::ClientContext &context, LogMessage *msg);
  std::shared_ptr<::grpc::ChannelCredentials> create_credentials();
  ::grpc::ChannelArguments create_channel_args();
  std::shared_ptr<::grpc::Channel>create_channel();

protected:
  GrpcDestWorker *super;
  DestDriver &owner;
  bool connected;
  std::shared_ptr<::grpc::Channel> channel;
};

}
}

#include "compat/cpp-start.h"

GrpcDestWorker *grpc_dw_new(GrpcDestDriver *o, gint worker_index);

struct GrpcDestWorker_
{
  LogThreadedDestWorker super;
  syslogng::grpc::DestWorker *cpp;
};

#include "compat/cpp-end.h"

#endif
