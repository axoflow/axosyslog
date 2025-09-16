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

#ifndef PUBSUB_DEST_WORKER_HPP
#define PUBSUB_DEST_WORKER_HPP

#include "pubsub-dest.hpp"
#include "grpc-dest-worker.hpp"

#include "google/pubsub/v1/pubsub.grpc.pb.h"

namespace syslogng {
namespace grpc {
namespace pubsub {

class DestWorker final : public syslogng::grpc::DestWorker
{
private:
  struct Slice
  {
    const char *str;
    std::size_t len;
  };

public:
  DestWorker(GrpcDestWorker *s);

  LogThreadedResult insert(LogMessage *msg);
  LogThreadedResult flush(LogThreadedFlushMode mode);

protected:
  bool init() override;
  void deinit() override;
  bool connect() override;

private:
  bool should_initiate_flush();
  void prepare_batch();
  const std::string format_topic(LogMessage *msg);
  DestWorker::Slice format_template(LogTemplate *tmpl, LogMessage *msg, GString *value, LogMessageValueType *type,
                                    gint seq_num) const;
  DestDriver *get_owner();

private:
  std::unique_ptr<::google::pubsub::v1::Publisher::Stub> stub;
  std::unique_ptr<::grpc::ClientContext> client_context;

  ::google::pubsub::v1::PublishRequest request;
  size_t batch_size = 0;
  size_t current_batch_bytes = 0;

  bool handle_protovar(LogMessage *msg, ::google::pubsub::v1::PubsubMessage *message, size_t *message_bytes);
  bool handle_data_attributes(LogMessage *msg, ::google::pubsub::v1::PubsubMessage *message, size_t *message_bytes);
};

}
}
}

#endif
