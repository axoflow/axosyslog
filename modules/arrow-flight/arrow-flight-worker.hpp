/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Attila Szakacs <attila.szakacs@axoflow.com>
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

#ifndef ARROW_FLIGHT_WORKER_HPP
#define ARROW_FLIGHT_WORKER_HPP

#include "arrow-flight-worker.h"
#include "arrow-flight-dest.hpp"

#include "compat/cpp-start.h"
#include "logthrdest/logthrdestdrv.h"
#include "scratch-buffers.h"
#include "compat/cpp-end.h"

#include <arrow/api.h>
#include <arrow/flight/api.h>

#include <memory>
#include <vector>

namespace syslog_ng {
namespace arrow_flight {

class DestinationWorker final
{
public:
  DestinationWorker(ArrowFlightDestWorker *s);
  ~DestinationWorker();

  bool init();
  void deinit();
  bool connect();
  void disconnect();
  LogThreadedResult insert(LogMessage *msg);
  LogThreadedResult flush(LogThreadedFlushMode mode);

private:
  DestinationDriver *get_owner();
  gint get_batch_size() const;
  const gchar *format_path(LogMessage *msg, GString **buf, ScratchBuffersMarker *marker);
  bool open_stream(const gchar *path);
  void close_stream();
  bool create_builders();
  bool append_value(arrow::ArrayBuilder *builder, const std::shared_ptr<arrow::DataType> &type,
                    const char *str, gssize len);

  ArrowFlightDestWorker *super;
  arrow::flight::Location location;
  std::unique_ptr<arrow::flight::FlightClient> client;
  std::vector<std::unique_ptr<arrow::ArrayBuilder>> builders;

  std::unique_ptr<arrow::flight::FlightStreamWriter> writer;
  std::unique_ptr<arrow::flight::FlightMetadataReader> metadata_reader;
  std::string current_stream_path;
  std::string current_batch_path;
};

}
}

#endif
