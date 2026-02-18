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

#ifndef CLICKHOUSE_DEST_WORKER_HPP
#define CLICKHOUSE_DEST_WORKER_HPP

#include "clickhouse-dest.hpp"
#include "grpc-dest-worker.hpp"

#include <sstream>

#include "clickhouse_grpc.grpc.pb.h"

namespace syslogng {
namespace grpc {
namespace clickhouse {

class DestWorker final : public syslogng::grpc::DestWorker
{
public:
  DestWorker(GrpcDestWorker *s);

  LogThreadedResult insert(LogMessage *msg) override;
  LogThreadedResult flush(LogThreadedFlushMode mode) override;

protected:
  bool connect() override;
  bool init() override;
  void deinit() override;

private:
  bool insert_query_data_from_protovar(LogMessage *msg);
  bool insert_query_data_from_jsonvar(LogMessage *msg);
  bool insert_query_data_from_schema(LogMessage *msg);
  bool should_initiate_flush();
  void prepare_query_info(::clickhouse::grpc::QueryInfo &query_info);
  void prepare_batch();
  DestDriver *get_owner();

private:
  std::unique_ptr<::clickhouse::grpc::ClickHouse::Stub> stub;
  std::unique_ptr<::grpc::ClientContext> client_context;

  std::ostringstream query_data;
  size_t batch_size = 0;
  size_t current_batch_bytes = 0;
};

}
}
}

#endif
