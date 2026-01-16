#!/usr/bin/env python
#############################################################################
# Copyright (c) 2026 Axoflow
# Copyright (c) 2026 Andras Mitzki <andras.mitzki@axoflow.com>
#
# This program is free software: you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.
#
# As an additional exemption you are allowed to compile & link against the
# OpenSSL libraries as published by the OpenSSL project. See the file
# COPYING for details.
#
#############################################################################
from collections import namedtuple

from axosyslog_light.common.blocking import wait_until_true
from axosyslog_light.helpers.loggen.loggen import LoggenStartParams
from axosyslog_light.syslog_ng_ctl.prometheus_stats_handler import MetricFilter


def set_config_with_default_non_reliable_disk_buffer_values(config, port_allocator, flow_control=True):
    config.update_global_options(stats_level=2)
    network_source = config.create_network_source(ip="localhost", port=port_allocator())
    network_destination = config.create_network_destination(ip="localhost", port=port_allocator(), time_reopen=1, disk_buffer={"reliable": "no", "dir": "'.'", "capacity-bytes": "1MiB", "front-cache-size": 1000})
    config.create_logpath(statements=[network_source, network_destination], flags="flow-control" if flow_control else "")
    return config, network_source, network_destination


def loggen_send_messages(loggen, network_source, number):
    loggen.start(LoggenStartParams(target=network_source.options["ip"], port=network_source.options["port"], inet=True, stream=True, size=1024, number=number))
    wait_until_true(lambda: loggen.get_sent_message_count() == number)
    loggen.stop()


def send_and_wait_for_messages_arrived(config, loggen, network_source, loggen_msg_number, expected_metrics):
    loggen_send_messages(loggen, network_source, number=loggen_msg_number)
    assert wait_until_true(lambda: config.get_prometheus_samples([MetricFilter("syslogng_disk_queue_processed_events_total", {})])[0].value == expected_metrics.syslogng_disk_queue_processed_events_total)


def fill_up_and_check_initial_disk_buffer_metrics(config, loggen, dqtool, network_source, loggen_msg_number, expected_metrics):
    loggen_send_messages(loggen, network_source, number=loggen_msg_number)
    validate_metrics(config, expected_metrics, abandoned=False, disk_buffer_file="./syslog-ng-00000.qf")
    if dqtool:
        validate_disk_buffer(dqtool, expected_metrics.messages_in_disk_buffer, disk_buffer_file="./syslog-ng-00000.qf")


def fill_up_and_check_new_disk_buffer_metrics(config, loggen, network_source, loggen_msg_number, expected_metrics):
    loggen_send_messages(loggen, network_source, number=loggen_msg_number)
    validate_metrics(config, expected_metrics, abandoned=False, disk_buffer_file="./syslog-ng-00001.qf")


def get_metric(config, metric_name, labels=None):
    return config.get_prometheus_samples([MetricFilter(metric_name, labels or {})])[0].value


def validate_metrics(config, expected_metrics, abandoned=False, disk_buffer_file="./syslog-ng-00000.qf"):
    labels = {"path": disk_buffer_file}
    if abandoned:
        labels["abandoned"] = "true"

    for metric_name, expected_value in expected_metrics._asdict().items():
        if metric_name.startswith("syslogng_"):
            actual_value = get_metric(config, metric_name, labels=labels)
            assert actual_value == expected_value, f"Metric {metric_name} expected: {expected_value}, actual: {actual_value}"
        if metric_name.startswith("delivered_") or metric_name.startswith("queued_") or metric_name.startswith("dropped_"):
            metric_prefix = metric_name.split("_")[0]
            metric_name = "_".join(metric_name.split("_")[1:])
            actual_value = get_metric(config, metric_name, labels={"result": metric_prefix})
            assert actual_value == expected_value, f"Metric {metric_name} with prefix {metric_prefix} expected: {expected_value}, actual: {actual_value}"


def check_abandoned_disk_buffer_metrics(config, first_buffer_metrics):
    validate_metrics(config, first_buffer_metrics, abandoned=True, disk_buffer_file="./syslog-ng-00000.qf")


def check_both_disk_buffer_metrics(config, first_buffer_metrics, second_buffer_metrics):
    check_abandoned_disk_buffer_metrics(config, first_buffer_metrics)
    validate_metrics(config, second_buffer_metrics, abandoned=False, disk_buffer_file="./syslog-ng-00001.qf")


def check_disk_buffer_metrics_after_destination_alive(config, dqtool, expected_metrics):
    validate_metrics(config, expected_metrics, abandoned=False, disk_buffer_file="./syslog-ng-00000.qf")
    validate_disk_buffer(dqtool, expected_metrics.messages_in_disk_buffer, disk_buffer_file="./syslog-ng-00000.qf")


def check_disk_buffer_metrics_after_restart_and_destination_alive(config, dqtool, expected_metrics):
    validate_metrics(config, expected_metrics, abandoned=False, disk_buffer_file="./syslog-ng-00000.qf")
    validate_disk_buffer(dqtool, expected_metrics.messages_in_disk_buffer, disk_buffer_file="./syslog-ng-00000.qf")


def check_disk_buffer_metrics_after_sending_more_logs_than_buffer_can_handle(config, dqtool, expected_metrics):
    validate_metrics(config, expected_metrics, abandoned=False, disk_buffer_file="./syslog-ng-00000.qf")
    validate_disk_buffer(dqtool, expected_metrics.messages_in_disk_buffer, disk_buffer_file="./syslog-ng-00000.qf")


def check_disk_buffer_metrics_after_reload(config, dqtool, expected_metrics):
    validate_metrics(config, expected_metrics, abandoned=False, disk_buffer_file="./syslog-ng-00000.qf")
    validate_disk_buffer(dqtool, expected_metrics.messages_in_disk_buffer, disk_buffer_file="./syslog-ng-00000.qf")


def validate_disk_buffer(dqtool, expected_message_count, disk_buffer_file="syslog-ng-00000.qf"):
    dqtool_info_output = dqtool.info(disk_buffer_file=disk_buffer_file)
    stderr_content = dqtool_info_output["stderr"]
    assert f"number_of_messages='{expected_message_count}'" in stderr_content, f"Expected {expected_message_count} messages in disk buffer, but got:\n{stderr_content}"


def check_disk_buffer_files(dqtool, expected_files_and_message_counts):
    for disk_buffer_file, expected_message_count in expected_files_and_message_counts:
        validate_disk_buffer(dqtool, expected_message_count, disk_buffer_file=disk_buffer_file)


def check_if_source_suspended(syslog_ng, is_suspended_source):
    assert syslog_ng.is_message_in_console_log("Source has been suspended") == is_suspended_source


def trigger_to_create_abandoned_disk_buffer(network_destination, config, port_allocator, syslog_ng):
    network_destination.options["port"] = port_allocator()
    network_destination.options["disk_buffer"]["front-cache-size"] = 2000
    syslog_ng.reload(config)
    return network_destination


def set_expected_metrics_state_when_sending_more_logs_than_buffer_can_handle_without_flow_control(frontCache_size, qdisk_size, extra_msg_number):
    BufferState = namedtuple(
        "BufferState", [
            "delivered_syslogng_output_events_total",
            "queued_syslogng_output_events_total",
            "dropped_syslogng_output_events_total",
            "syslogng_disk_queue_processed_events_total",
            "syslogng_disk_queue_disk_allocated_bytes",
            "syslogng_disk_queue_disk_usage_bytes",
            "syslogng_disk_queue_events",
            "syslogng_disk_queue_memory_usage_bytes",
            "messages_in_disk_buffer",
        ],
    )
    TCParams = namedtuple(
        "TCParams", [
            "last_msg_id", "before", "after",
        ],
    )
    return TCParams(
        last_msg_id=f"000000{frontCache_size + qdisk_size - 1}",
        before=BufferState(
            delivered_syslogng_output_events_total=0,
            queued_syslogng_output_events_total=frontCache_size + qdisk_size,
            dropped_syslogng_output_events_total=extra_msg_number,
            syslogng_disk_queue_processed_events_total=frontCache_size + qdisk_size + extra_msg_number,
            syslogng_disk_queue_disk_allocated_bytes=1048576,
            syslogng_disk_queue_disk_usage_bytes=1044480,
            syslogng_disk_queue_events=frontCache_size + qdisk_size,
            syslogng_disk_queue_memory_usage_bytes=764000,
            messages_in_disk_buffer=qdisk_size,
        ),
        after=BufferState(
            delivered_syslogng_output_events_total=frontCache_size + qdisk_size,
            queued_syslogng_output_events_total=0,
            dropped_syslogng_output_events_total=extra_msg_number,
            syslogng_disk_queue_processed_events_total=frontCache_size + qdisk_size + extra_msg_number,
            syslogng_disk_queue_disk_allocated_bytes=1048576,
            syslogng_disk_queue_disk_usage_bytes=0,
            syslogng_disk_queue_events=0,
            syslogng_disk_queue_memory_usage_bytes=0,
            messages_in_disk_buffer=0,
        ),
    )


def set_expected_metrics_state_when_sending_more_logs_than_buffer_can_handle_with_flow_control(frontCache_size, qdisk_size, flow_control_window_size, extra_msg_number):
    BufferState = namedtuple(
        "BufferState", [
            "delivered_syslogng_output_events_total",
            "queued_syslogng_output_events_total",
            "dropped_syslogng_output_events_total",
            "syslogng_disk_queue_processed_events_total",
            "syslogng_disk_queue_disk_allocated_bytes",
            "syslogng_disk_queue_disk_usage_bytes",
            "syslogng_disk_queue_events",
            # "syslogng_disk_queue_memory_usage_bytes",
            "messages_in_disk_buffer",
        ],
    )
    TCParams = namedtuple(
        "TCParams", [
            "last_msg_id", "is_suspended_source", "before_reload", "after_reload", "after_dst_alive",
        ],
    )
    return TCParams(
        last_msg_id=f"000000{frontCache_size + qdisk_size + flow_control_window_size + extra_msg_number - 1}",
        is_suspended_source=True,
        before_reload=BufferState(
            delivered_syslogng_output_events_total=0,
            queued_syslogng_output_events_total=frontCache_size + qdisk_size + flow_control_window_size,
            dropped_syslogng_output_events_total=0,
            syslogng_disk_queue_processed_events_total=1324,
            syslogng_disk_queue_disk_allocated_bytes=1048576,
            syslogng_disk_queue_disk_usage_bytes=1044480,
            syslogng_disk_queue_events=1324,
            # syslogng_disk_queue_memory_usage_bytes=916800,
            messages_in_disk_buffer=724,
        ),
        after_reload=BufferState(
            delivered_syslogng_output_events_total=0,
            queued_syslogng_output_events_total=frontCache_size + qdisk_size + flow_control_window_size + flow_control_window_size,
            dropped_syslogng_output_events_total=0,
            syslogng_disk_queue_processed_events_total=frontCache_size + qdisk_size + flow_control_window_size + flow_control_window_size,
            syslogng_disk_queue_disk_allocated_bytes=1048576,
            syslogng_disk_queue_disk_usage_bytes=1044480,
            syslogng_disk_queue_events=frontCache_size + qdisk_size + flow_control_window_size + flow_control_window_size,
            # syslogng_disk_queue_memory_usage_bytes=916800,
            messages_in_disk_buffer=724,
        ),
        after_dst_alive=BufferState(
            delivered_syslogng_output_events_total=frontCache_size + qdisk_size + flow_control_window_size + extra_msg_number,
            queued_syslogng_output_events_total=0,
            dropped_syslogng_output_events_total=0,
            syslogng_disk_queue_processed_events_total=frontCache_size + qdisk_size + flow_control_window_size + extra_msg_number,
            syslogng_disk_queue_disk_allocated_bytes=1048576,
            syslogng_disk_queue_disk_usage_bytes=0,
            syslogng_disk_queue_events=0,
            # syslogng_disk_queue_memory_usage_bytes=0,
            messages_in_disk_buffer=0,
        ),
    )
