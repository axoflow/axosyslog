/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 László Várady
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

#ifndef METRIC_NAMES_H
#define METRIC_NAMES_H

#include "syslog-ng.h"

#define METRIC_PREFIX "syslogng_"
#define METRIC(name) SYSLOG_NG_METRIC_NAMES[METRIC_##name]

#define METRIC_NAMES(M) \
  M(classified_events_total) \
  M(disk_queue_capacity_bytes) \
  M(disk_queue_capacity) \
  M(disk_queue_dir_available_bytes) \
  M(disk_queue_disk_allocated_bytes) \
  M(disk_queue_disk_usage_bytes) \
  M(disk_queue_events) \
  M(disk_queue_memory_usage_bytes) \
  M(disk_queue_processed_events_total) \
  M(event_processing_latency_seconds) \
  M(events_allocated_bytes) \
  M(filtered_events_total) \
  M(fx_xxx_evals_total) \
  M(input_event_bytes_total) \
  M(input_events_total) \
  M(input_window_available) \
  M(input_window_capacity) \
  M(input_window_full_total) \
  M(internal_events_queue_capacity) \
  M(internal_events_total) \
  M(io_worker_latency_seconds) \
  M(last_config_file_modification_timestamp_seconds) \
  M(last_config_reload_timestamp_seconds) \
  M(last_successful_config_reload_timestamp_seconds) \
  M(mainloop_io_worker_roundtrip_latency_seconds) \
  M(memory_queue_capacity) \
  M(memory_queue_events) \
  M(memory_queue_memory_usage_bytes) \
  M(memory_queue_processed_events_total) \
  M(output_active_worker_partitions) \
  M(output_batch_size_bytes) \
  M(output_batch_size_events) \
  M(output_batch_timedout_total) \
  M(output_event_bytes_total) \
  M(output_event_latency_seconds) \
  M(output_event_retries_total) \
  M(output_event_size_bytes) \
  M(output_events_total) \
  M(output_grpc_requests_total) \
  M(output_http_requests_total) \
  M(output_request_latency_seconds) \
  M(output_unreachable) \
  M(output_workers) \
  M(parallelize_failed_events_total) \
  M(parallelized_assigned_events_total) \
  M(parallelized_processed_events_total) \
  M(parsed_events_total) \
  M(processed_events_total) \
  M(route_egress_total) \
  M(route_ingress_total) \
  M(scratch_buffers_bytes) \
  M(scratch_buffers_count) \
  M(socket_connections) \
  M(socket_max_connections) \
  M(socket_receive_buffer_max_bytes) \
  M(socket_receive_buffer_used_bytes) \
  M(socket_receive_dropped_packets_total) \
  M(socket_rejected_connections_total) \
  M(stats_level) \
  M(tagged_events_total)

#define TO_ENUM_ELEM(name) METRIC_##name,
#define TO_STRING_ELEM(name) #name,

typedef enum _MetricNames
{
  METRIC_NAMES(TO_ENUM_ELEM)
  METRIC_NAMES_MAX
} MetricNames;

extern const gchar *SYSLOG_NG_METRIC_NAMES[];

#endif
