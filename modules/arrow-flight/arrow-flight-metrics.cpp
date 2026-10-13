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

#include "arrow-flight-metrics.hpp"

#include "compat/cpp-start.h"
#include "stats/stats-cluster-single.h"
#include "compat/cpp-end.h"

#include <arrow/flight/types.h>

#include <vector>

using syslog_ng::arrow_flight::DestDriverMetrics;

template <typename Code>
struct ResponseCodeLabel
{
  Code code;
  const char *label;
};

static const ResponseCodeLabel<arrow::flight::FlightStatusCode> flight_status_code_labels[] =
{
  { arrow::flight::FlightStatusCode::Internal, "internal" },
  { arrow::flight::FlightStatusCode::TimedOut, "timed_out" },
  { arrow::flight::FlightStatusCode::Cancelled, "cancelled" },
  { arrow::flight::FlightStatusCode::Unauthenticated, "unauthenticated" },
  { arrow::flight::FlightStatusCode::Unauthorized, "unauthorized" },
  { arrow::flight::FlightStatusCode::Unavailable, "unavailable" },
  { arrow::flight::FlightStatusCode::Failed, "failed" },
};

static const ResponseCodeLabel<arrow::StatusCode> arrow_status_code_labels[] =
{
  { arrow::StatusCode::OutOfMemory, "out_of_memory" },
  { arrow::StatusCode::KeyError, "key_error" },
  { arrow::StatusCode::TypeError, "type_error" },
  { arrow::StatusCode::Invalid, "invalid" },
  { arrow::StatusCode::IOError, "io_error" },
  { arrow::StatusCode::CapacityError, "capacity_error" },
  { arrow::StatusCode::IndexError, "index_error" },
  { arrow::StatusCode::Cancelled, "cancelled" },
  { arrow::StatusCode::NotImplemented, "not_implemented" },
  { arrow::StatusCode::SerializationError, "serialization_error" },
  { arrow::StatusCode::AlreadyExists, "already_exists" },
};

template <typename Code, size_t N>
static const char *
_lookup_label(const ResponseCodeLabel<Code> (&table)[N], Code code)
{
  for (const auto &entry : table)
    {
      if (entry.code == code)
        return entry.label;
    }
  return "unknown_error";
}

static const char *
_response_code_label(const arrow::Status &status)
{
  if (status.ok())
    return "ok";

  auto flight_detail = arrow::flight::FlightStatusDetail::UnwrapStatus(status);
  if (flight_detail)
    return _lookup_label(flight_status_code_labels, flight_detail->code());

  return _lookup_label(arrow_status_code_labels, status.code());
}

/* Takes the ownership of kb. */
void
DestDriverMetrics::init(StatsClusterKeyBuilder *kb_, int stats_level)
{
  std::vector<const char *> labels = { "ok", "unknown_error" };
  for (const auto &entry : flight_status_code_labels)
    labels.push_back(entry.label);
  for (const auto &entry : arrow_status_code_labels)
    labels.push_back(entry.label);

  kb = kb_;

  stats_lock();
  for (const char *label : labels)
    {
      if (request_clusters.count(label))
        continue;

      stats_cluster_key_builder_push(kb);
      stats_cluster_key_builder_set_name(kb, METRIC(output_grpc_requests_total));
      stats_cluster_key_builder_add_label(kb, stats_cluster_label("response_code", label));
      StatsClusterKey *sc_key = stats_cluster_key_builder_build_single(kb);

      StatsCounterItem *counter;
      request_clusters[label] = stats_register_counter(stats_level, sc_key, SC_TYPE_SINGLE_VALUE, &counter);

      stats_cluster_key_free(sc_key);
      stats_cluster_key_builder_pop(kb);
    }
  stats_unlock();
}

void
DestDriverMetrics::deinit()
{
  stats_lock();
  for (const auto &cluster : request_clusters)
    {
      StatsCounterItem *counter = stats_cluster_single_get_counter(cluster.second);
      stats_unregister_counter(&cluster.second->key, SC_TYPE_SINGLE_VALUE, &counter);
    }
  stats_unlock();
  request_clusters.clear();

  stats_cluster_key_builder_free(kb);
  kb = nullptr;
}

void
DestDriverMetrics::insert_request_stats(const arrow::Status &status)
{
  stats_counter_inc(stats_cluster_single_get_counter(request_clusters.at(_response_code_label(status))));
}
