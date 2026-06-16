/*
 * Copyright (c) 2026 Axoflow
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

#include <criterion/criterion.h>

#include "bigquery-worker.hpp"

using syslogng::grpc::bigquery::bigquery_should_half_close_writer;

/*
 * The BigQuery destination opens a single bidirectional AppendRows streaming
 * call (batch_writer) per worker. disconnect() normally half-closes it with
 * WritesDone() + Finish(). When the server restarts the stream, the in-flight
 * Write()/Read() fails and the call reaches a terminal state; queuing another
 * terminal operation on it makes gRPC core abort the process with
 * GRPC_CALL_ERROR_TOO_MANY_OPERATIONS (exit 139). disconnect() must therefore
 * skip the handshake once the writer has failed.
 *
 * The half-close decision is factored into bigquery_should_half_close_writer()
 * so the invariant can be checked without a live gRPC stream.
 */

Test(bigquery_worker, half_close_runs_on_healthy_connected_writer)
{
  cr_assert(bigquery_should_half_close_writer(/* connected */ true,
                                              /* batch_writer_failed */ false),
            "a healthy, connected writer must be half-closed normally");
}

Test(bigquery_worker, half_close_skipped_after_write_failure)
{
  cr_assert_not(bigquery_should_half_close_writer(/* connected */ true,
                                                  /* batch_writer_failed */ true),
                "a writer whose stream has failed must not be half-closed "
                "(would trip GRPC_CALL_ERROR_TOO_MANY_OPERATIONS)");
}

Test(bigquery_worker, half_close_skipped_when_not_connected)
{
  cr_assert_not(bigquery_should_half_close_writer(/* connected */ false,
                                                  /* batch_writer_failed */ false),
                "a disconnected worker has no streaming call to half-close");
}

Test(bigquery_worker, half_close_skipped_when_not_connected_and_failed)
{
  cr_assert_not(bigquery_should_half_close_writer(/* connected */ false,
                                                  /* batch_writer_failed */ true),
                "neither condition permits the half-close handshake");
}
