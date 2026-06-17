/*
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

#ifndef BIGQUERY_WORKER_HPP
#define BIGQUERY_WORKER_HPP

#include "bigquery-dest.hpp"
#include "grpc-dest-worker.hpp"

#include "compat/cpp-start.h"
#include "messages.h"
#include "compat/cpp-end.h"

#include <grpcpp/create_channel.h>

#include <string>
#include <memory>
#include <cstddef>

#include "google/cloud/bigquery/storage/v1/storage.grpc.pb.h"

namespace syslogng {
namespace grpc {
namespace bigquery {

/*
 * Whether disconnect() may run the WritesDone()/Finish() half-close handshake
 * on the AppendRows streaming call. It is only safe while the worker is
 * connected and the streaming call has not already failed: issuing a terminal
 * operation on a call that has already completed makes gRPC core abort the
 * process with GRPC_CALL_ERROR_TOO_MANY_OPERATIONS. Kept as a free function so
 * the invariant can be unit tested without a live gRPC stream.
 */
static inline bool
bigquery_should_half_close_writer(bool connected, bool batch_writer_failed)
{
  return connected && !batch_writer_failed;
}

class DestinationWorker final : public syslogng::grpc::DestWorker
{
private:
  struct Slice
  {
    const char *str;
    std::size_t len;
  };

public:
  DestinationWorker(GrpcDestWorker *s);
  ~DestinationWorker();

  LogThreadedResult insert(LogMessage *msg) override;
  LogThreadedResult flush(LogThreadedFlushMode mode) override;

protected:
  bool connect() override;
  void disconnect() override;
  bool init() override;
  void deinit() override;
  bool connected;

private:
  std::shared_ptr<::grpc::Channel> create_channel();
  bool construct_write_stream();
  void prepare_batch();
  bool should_initiate_flush();
  LogThreadedResult handle_row_errors(const google::cloud::bigquery::storage::v1::AppendRowsResponse &response);
  DestinationDriver *get_owner();

private:
  std::string table;
  std::unique_ptr<google::cloud::bigquery::storage::v1::BigQueryWrite::Stub> stub;

  google::cloud::bigquery::storage::v1::WriteStream write_stream;
  std::unique_ptr<::grpc::ClientContext> batch_writer_ctx;
  std::unique_ptr<::grpc::ClientReaderWriter<google::cloud::bigquery::storage::v1::AppendRowsRequest,
      google::cloud::bigquery::storage::v1::AppendRowsResponse>> batch_writer;

  /*
   * Set when a Write()/Read() on batch_writer fails (e.g. the server restarts
   * the AppendRows stream). Once the streaming call has failed, the half-close
   * handshake (WritesDone() + Finish()) must not be issued on it: gRPC core
   * aborts the process with GRPC_CALL_ERROR_TOO_MANY_OPERATIONS if a terminal
   * operation is queued on a call that has already completed. disconnect()
   * checks this flag to skip that handshake on a broken writer.
   */
  bool batch_writer_failed = false;

  /* batch state */
  google::cloud::bigquery::storage::v1::AppendRowsRequest *current_batch;
  google::cloud::bigquery::storage::v1::AppendRowsResponse *append_rows_response;
  size_t batch_size = 0;
  size_t current_batch_bytes = 0;
};

}
}
}

#endif
