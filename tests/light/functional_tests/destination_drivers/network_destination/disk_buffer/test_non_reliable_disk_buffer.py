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

import pytest
from helper_functions import buffer_params
from helper_functions import check_disk_buffer_metrics_after_destination_alive
from helper_functions import check_disk_buffer_metrics_after_reload
from helper_functions import check_disk_buffer_metrics_after_restart_and_destination_alive
from helper_functions import check_disk_buffer_metrics_after_sending_more_logs_than_buffer_can_handle
from helper_functions import check_disk_buffer_state_load_attempts
from helper_functions import check_disk_buffer_state_save_attempts
from helper_functions import check_if_source_suspended
from helper_functions import fill_up_and_check_initial_disk_buffer_metrics
from helper_functions import get_metric
from helper_functions import loggen_send_messages
from helper_functions import send_and_wait_for_messages_arrived
from helper_functions import set_config_with_default_non_reliable_disk_buffer_values
from helper_functions import set_expected_metrics_state_when_sending_more_logs_than_buffer_can_handle_with_flow_control
from helper_functions import set_expected_metrics_state_when_sending_more_logs_than_buffer_can_handle_without_flow_control
from helper_functions import validate_disk_buffer


def test_expected_message_size_in_memory_match_current_reality(config, port_allocator, syslog_ng, loggen, dqtool):
    config, network_source, network_destination = set_config_with_default_non_reliable_disk_buffer_values(config, port_allocator)

    syslog_ng.start(config)
    # single message goes directly to qout, so stays in memory
    loggen_send_messages(loggen, network_source, number=1)
    size_of_message_in_memory = get_metric(config, "syslogng_disk_queue_memory_usage_bytes")
    syslog_ng.stop()
    assert size_of_message_in_memory == buffer_params.message_size_in_memory


def test_expected_message_size_in_diskq_match_current_reality(config, port_allocator, syslog_ng, loggen, dqtool):
    config, network_source, network_destination = set_config_with_default_non_reliable_disk_buffer_values(config, port_allocator)

    syslog_ng.start(config)
    # 500 messages go to qout (stays in memory), and the rest goes to diskq
    #
    # NOTE: our disk usage metric has a granularity of 1kB, everything else is rounded DOWN
    # we need store enough messages that are _exactly_ on a kB boundary.
    # OR we need to fix the disk_usage metric

    messages_to_measure = 256

    assert (messages_to_measure * buffer_params.message_size_in_diskq) % 1024 == 0
    loggen_send_messages(loggen, network_source, number=buffer_params.count_front_cache + messages_to_measure)
    size_of_message_in_diskq = get_metric(config, "syslogng_disk_queue_disk_usage_bytes")
    syslog_ng.stop()
    assert size_of_message_in_diskq == messages_to_measure * buffer_params.message_size_in_diskq


BufferState = namedtuple(
    "BufferState", [
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
        "loggen_msg_number", "last_msg_id", "is_suspended_source", "before", "after",
    ],
)


@pytest.mark.parametrize(
    "params", [
        TCParams(
            # FrontCache_MAX = 500, QDISK_MAX = 724, WINDOW_MAX = 100, 1 msg raw size = 1024 bytes, 1 msg nvtable size = 1528 bytes
            # [FrontCache_MAX-1]-[0 QDISK]-[0 WINDOW]
            loggen_msg_number=buffer_params.count_front_cache - 1,
            last_msg_id=f"{buffer_params.count_front_cache - 2:010}",
            is_suspended_source=False,
            before=BufferState(
                syslogng_disk_queue_processed_events_total=buffer_params.count_front_cache - 1,
                syslogng_disk_queue_disk_allocated_bytes=4096,
                syslogng_disk_queue_disk_usage_bytes=0,
                syslogng_disk_queue_events=buffer_params.count_front_cache - 1,
                syslogng_disk_queue_memory_usage_bytes=(buffer_params.count_front_cache - 1) * buffer_params.message_size_in_memory,
                messages_in_disk_buffer=0,
            ),
            after=BufferState(
                syslogng_disk_queue_processed_events_total=buffer_params.count_front_cache - 1,
                syslogng_disk_queue_disk_allocated_bytes=4096,
                syslogng_disk_queue_disk_usage_bytes=0,
                syslogng_disk_queue_events=0,
                syslogng_disk_queue_memory_usage_bytes=0,
                messages_in_disk_buffer=0,
            ),
        ),
        TCParams(
            # [FrontCache_MAX]-[0 QDISK]-[0 WINDOW]
            loggen_msg_number=buffer_params.count_front_cache,
            last_msg_id=f"{buffer_params.count_front_cache - 1:010}",
            is_suspended_source=False,
            before=BufferState(
                syslogng_disk_queue_processed_events_total=buffer_params.count_front_cache,
                syslogng_disk_queue_disk_allocated_bytes=4096,
                syslogng_disk_queue_disk_usage_bytes=0,
                syslogng_disk_queue_events=buffer_params.count_front_cache,
                syslogng_disk_queue_memory_usage_bytes=buffer_params.count_front_cache * buffer_params.message_size_in_memory,
                messages_in_disk_buffer=0,
            ),
            after=BufferState(
                syslogng_disk_queue_processed_events_total=buffer_params.count_front_cache,
                syslogng_disk_queue_disk_allocated_bytes=4096,
                syslogng_disk_queue_disk_usage_bytes=0,
                syslogng_disk_queue_events=0,
                syslogng_disk_queue_memory_usage_bytes=0,
                messages_in_disk_buffer=0,
            ),
        ),
        TCParams(
            # [FrontCache_MAX]-[1 QDISK]-[0 WINDOW]
            loggen_msg_number=buffer_params.count_front_cache + 1,
            last_msg_id=f"{buffer_params.count_front_cache:010}",
            is_suspended_source=False,
            before=BufferState(
                syslogng_disk_queue_processed_events_total=buffer_params.count_front_cache + 1,
                syslogng_disk_queue_disk_allocated_bytes=4096 + 1024,
                syslogng_disk_queue_disk_usage_bytes=1024,
                syslogng_disk_queue_events=buffer_params.count_front_cache + 1,
                syslogng_disk_queue_memory_usage_bytes=buffer_params.count_front_cache * buffer_params.message_size_in_memory,
                messages_in_disk_buffer=1,
            ),
            after=BufferState(
                syslogng_disk_queue_processed_events_total=buffer_params.count_front_cache + 1,
                syslogng_disk_queue_disk_allocated_bytes=4096 + 1024,
                syslogng_disk_queue_disk_usage_bytes=0,
                syslogng_disk_queue_events=0,
                syslogng_disk_queue_memory_usage_bytes=0,
                messages_in_disk_buffer=0,
            ),
        ),
        TCParams(
            # [FrontCache_MAX]-[QDISK_MAX-1]-[0 WINDOW]
            loggen_msg_number=buffer_params.count_front_cache + buffer_params.count_qdisk - 1,
            last_msg_id=f"{buffer_params.count_front_cache + buffer_params.count_qdisk - 2:010}",
            is_suspended_source=False,
            before=BufferState(
                syslogng_disk_queue_processed_events_total=buffer_params.count_front_cache + buffer_params.count_qdisk - 1,
                syslogng_disk_queue_disk_allocated_bytes=1047552,
                syslogng_disk_queue_disk_usage_bytes=1043456,
                syslogng_disk_queue_events=buffer_params.count_front_cache + buffer_params.count_qdisk - 1,
                syslogng_disk_queue_memory_usage_bytes=buffer_params.count_front_cache * buffer_params.message_size_in_memory,
                messages_in_disk_buffer=buffer_params.count_qdisk - 1,
            ),
            after=BufferState(
                syslogng_disk_queue_processed_events_total=buffer_params.count_front_cache + buffer_params.count_qdisk - 1,
                syslogng_disk_queue_disk_allocated_bytes=1047552,
                syslogng_disk_queue_disk_usage_bytes=0,
                syslogng_disk_queue_events=0,
                syslogng_disk_queue_memory_usage_bytes=0,
                messages_in_disk_buffer=0,
            ),
        ),
        TCParams(
            # [FrontCache_MAX]-[QDISK_MAX]-[0 WINDOW]
            loggen_msg_number=buffer_params.count_front_cache + buffer_params.count_qdisk,
            last_msg_id=f"{buffer_params.count_front_cache + buffer_params.count_qdisk - 1:010}",
            is_suspended_source=False,
            before=BufferState(
                syslogng_disk_queue_processed_events_total=buffer_params.count_front_cache + buffer_params.count_qdisk,
                syslogng_disk_queue_disk_allocated_bytes=1048576,
                syslogng_disk_queue_disk_usage_bytes=1044480,
                syslogng_disk_queue_events=buffer_params.count_front_cache + buffer_params.count_qdisk,
                syslogng_disk_queue_memory_usage_bytes=buffer_params.count_front_cache * buffer_params.message_size_in_memory,
                messages_in_disk_buffer=buffer_params.count_qdisk,
            ),
            after=BufferState(
                syslogng_disk_queue_processed_events_total=buffer_params.count_front_cache + buffer_params.count_qdisk,
                syslogng_disk_queue_disk_allocated_bytes=1048576,
                syslogng_disk_queue_disk_usage_bytes=0,
                syslogng_disk_queue_events=0,
                syslogng_disk_queue_memory_usage_bytes=0,
                messages_in_disk_buffer=0,
            ),
        ),
        TCParams(
            # [FrontCache_MAX]-[QDISK_MAX]-[1 WINDOW]
            loggen_msg_number=buffer_params.count_front_cache + buffer_params.count_qdisk + 1,
            last_msg_id=f"{buffer_params.count_front_cache + buffer_params.count_qdisk:010}",
            is_suspended_source=False,
            before=BufferState(
                syslogng_disk_queue_processed_events_total=buffer_params.count_front_cache + buffer_params.count_qdisk + 1,
                syslogng_disk_queue_disk_allocated_bytes=1048576,
                syslogng_disk_queue_disk_usage_bytes=1044480,
                syslogng_disk_queue_events=buffer_params.count_front_cache + buffer_params.count_qdisk + 1,
                syslogng_disk_queue_memory_usage_bytes=(buffer_params.count_front_cache + 1) * buffer_params.message_size_in_memory,
                messages_in_disk_buffer=buffer_params.count_qdisk,
            ),
            after=BufferState(
                syslogng_disk_queue_processed_events_total=buffer_params.count_front_cache + buffer_params.count_qdisk + 1,
                syslogng_disk_queue_disk_allocated_bytes=1048576,
                syslogng_disk_queue_disk_usage_bytes=0,
                syslogng_disk_queue_events=0,
                syslogng_disk_queue_memory_usage_bytes=0,
                messages_in_disk_buffer=0,
            ),
        ),
        TCParams(
            # [FrontCache_MAX]-[QDISK_MAX]-[WINDOW_MAX-1]
            loggen_msg_number=buffer_params.count_front_cache + buffer_params.count_qdisk + buffer_params.count_flow_control_window - 1,
            last_msg_id=f"{buffer_params.count_front_cache + buffer_params.count_qdisk + buffer_params.count_flow_control_window - 2:010}",
            is_suspended_source=False,
            before=BufferState(
                syslogng_disk_queue_processed_events_total=buffer_params.count_front_cache + buffer_params.count_qdisk + buffer_params.count_flow_control_window - 1,
                syslogng_disk_queue_disk_allocated_bytes=1048576,
                syslogng_disk_queue_disk_usage_bytes=1044480,
                syslogng_disk_queue_events=buffer_params.count_front_cache + buffer_params.count_qdisk + buffer_params.count_flow_control_window - 1,
                syslogng_disk_queue_memory_usage_bytes=(buffer_params.count_front_cache + buffer_params.count_flow_control_window - 1) * buffer_params.message_size_in_memory,
                messages_in_disk_buffer=buffer_params.count_qdisk,
            ),
            after=BufferState(
                syslogng_disk_queue_processed_events_total=buffer_params.count_front_cache + buffer_params.count_qdisk + buffer_params.count_flow_control_window - 1,
                syslogng_disk_queue_disk_allocated_bytes=1048576,
                syslogng_disk_queue_disk_usage_bytes=0,
                syslogng_disk_queue_events=0,
                syslogng_disk_queue_memory_usage_bytes=0,
                messages_in_disk_buffer=0,
            ),
        ),
        TCParams(
            # [FrontCache_MAX]-[QDISK_MAX]-[WINDOW_MAX]
            loggen_msg_number=buffer_params.count_front_cache + buffer_params.count_qdisk + buffer_params.count_flow_control_window,
            last_msg_id=f"{buffer_params.count_front_cache + buffer_params.count_qdisk + buffer_params.count_flow_control_window - 1:010}",
            is_suspended_source=True,
            before=BufferState(
                syslogng_disk_queue_processed_events_total=buffer_params.count_front_cache + buffer_params.count_qdisk + buffer_params.count_flow_control_window,
                syslogng_disk_queue_disk_allocated_bytes=1048576,
                syslogng_disk_queue_disk_usage_bytes=1044480,
                syslogng_disk_queue_events=buffer_params.count_front_cache + buffer_params.count_qdisk + buffer_params.count_flow_control_window,
                syslogng_disk_queue_memory_usage_bytes=(buffer_params.count_front_cache + buffer_params.count_flow_control_window) * buffer_params.message_size_in_memory,
                messages_in_disk_buffer=buffer_params.count_qdisk,
            ),
            after=BufferState(
                syslogng_disk_queue_processed_events_total=buffer_params.count_front_cache + buffer_params.count_qdisk + buffer_params.count_flow_control_window,
                syslogng_disk_queue_disk_allocated_bytes=1048576,
                syslogng_disk_queue_disk_usage_bytes=0,
                syslogng_disk_queue_events=0,
                syslogng_disk_queue_memory_usage_bytes=0,
                messages_in_disk_buffer=0,
            ),
        ),
    ], )
def test_fill_up_buffers_for_non_reliable_disk_buffer_with_flow_control_then_send_out_all_logs(config, port_allocator, syslog_ng, loggen, dqtool, params):
    config, network_source, network_destination = set_config_with_default_non_reliable_disk_buffer_values(config, port_allocator)

    syslog_ng.start(config)
    fill_up_and_check_initial_disk_buffer_metrics(config, loggen, dqtool, network_source, params.loggen_msg_number, params.before)

    syslog_ng.reload(config)
    check_disk_buffer_metrics_after_reload(config, dqtool, params.before)

    network_destination.start_listener()
    assert network_destination.read_until_logs([f"seq: {params.last_msg_id}"])
    check_disk_buffer_metrics_after_destination_alive(config, dqtool, params.after)

    syslog_ng.stop()
    validate_disk_buffer(dqtool, 0, disk_buffer_file="./syslog-ng-00000.qf")
    check_if_source_suspended(syslog_ng, params.is_suspended_source)


@pytest.mark.parametrize(
    "params", [
        TCParams(
            # [FrontCache_MAX]-[QDISK_MAX]-[WINDOW_MAX]
            loggen_msg_number=buffer_params.count_front_cache + buffer_params.count_qdisk + buffer_params.count_flow_control_window,
            last_msg_id=f"{buffer_params.count_front_cache + buffer_params.count_qdisk + buffer_params.count_flow_control_window - 1:010}",
            is_suspended_source=None,
            before=BufferState(
                syslogng_disk_queue_processed_events_total=buffer_params.count_front_cache + buffer_params.count_qdisk + buffer_params.count_flow_control_window,
                syslogng_disk_queue_disk_allocated_bytes=None,
                syslogng_disk_queue_disk_usage_bytes=None,
                syslogng_disk_queue_events=buffer_params.count_front_cache + buffer_params.count_qdisk + buffer_params.count_flow_control_window,
                syslogng_disk_queue_memory_usage_bytes=(buffer_params.count_front_cache + buffer_params.count_flow_control_window) * buffer_params.message_size_in_memory,
                messages_in_disk_buffer=buffer_params.count_qdisk,
            ),
            after=BufferState(
                syslogng_disk_queue_processed_events_total=0,
                syslogng_disk_queue_disk_allocated_bytes=1048576,
                syslogng_disk_queue_disk_usage_bytes=0,
                syslogng_disk_queue_events=0,
                syslogng_disk_queue_memory_usage_bytes=0,
                messages_in_disk_buffer=0,
            ),
        ),
    ], )
def test_fill_up_buffers_for_non_reliable_disk_buffer_with_flow_control_then_after_a_restart_send_out_all_logs(config, port_allocator, syslog_ng, loggen, dqtool, params):
    config, network_source, network_destination = set_config_with_default_non_reliable_disk_buffer_values(config, port_allocator)

    syslog_ng.start(config)
    send_and_wait_for_messages_arrived(config, loggen, network_source, params.loggen_msg_number, params.before)

    syslog_ng.stop()
    validate_disk_buffer(dqtool, params.loggen_msg_number, disk_buffer_file="./syslog-ng-00000.qf")

    network_destination.start_listener()
    syslog_ng.start(config)
    assert network_destination.read_until_logs([f"seq: {params.last_msg_id}"])
    check_disk_buffer_metrics_after_restart_and_destination_alive(config, dqtool, params.after)

    syslog_ng.stop()


def test_overwrite_buffers_with_reload_for_non_reliable_disk_buffer_with_flow_control_then_send_out_all_logs(config, port_allocator, syslog_ng, loggen, dqtool):
    # reload will cause the overwrite of the existing buffers, with reading extra logs into flow control window
    config, network_source, network_destination = set_config_with_default_non_reliable_disk_buffer_values(config, port_allocator)

    extra_msg_number = 1000  # will be forwarded too
    loggen_msg_counter_to_overflow_buffer = buffer_params.count_front_cache + buffer_params.count_qdisk + buffer_params.count_flow_control_window + extra_msg_number
    params = set_expected_metrics_state_when_sending_more_logs_than_buffer_can_handle_with_flow_control(buffer_params.count_front_cache, buffer_params.count_qdisk, buffer_params.count_flow_control_window, extra_msg_number)

    syslog_ng.start(config)
    fill_up_and_check_initial_disk_buffer_metrics(config, loggen, dqtool, network_source, loggen_msg_counter_to_overflow_buffer, params.before_reload)

    syslog_ng.reload(config)
    # axosyslog will read an extra buffer_params.count_flow_control_window messages into the flow control window
    check_disk_buffer_metrics_after_reload(config, dqtool, params.after_reload)

    network_destination.start_listener()
    assert network_destination.read_until_logs([f"seq: {params.last_msg_id}"])
    check_disk_buffer_metrics_after_destination_alive(config, dqtool, params.after_dst_alive)

    syslog_ng.stop()
    validate_disk_buffer(dqtool, 0, disk_buffer_file="./syslog-ng-00000.qf")
    check_if_source_suspended(syslog_ng, params.is_suspended_source)
    check_disk_buffer_state_load_attempts(syslog_ng, expected_load_attempts=2)
    check_disk_buffer_state_save_attempts(syslog_ng, expected_save_attempts=2)


def test_fill_up_buffers_for_non_reliable_disk_buffer_without_flow_control_then_send_out_all_logs(config, port_allocator, syslog_ng, loggen, dqtool):
    config, network_source, network_destination = set_config_with_default_non_reliable_disk_buffer_values(config, port_allocator, flow_control=False)

    syslog_ng.start(config)
    extra_msg_number = 1000  # will be dropped
    loggen_msg_counter_to_overflow_buffer = buffer_params.count_front_cache + buffer_params.count_qdisk + extra_msg_number
    loggen_send_messages(loggen, network_source, number=loggen_msg_counter_to_overflow_buffer)

    expected_metrics_state = set_expected_metrics_state_when_sending_more_logs_than_buffer_can_handle_without_flow_control(buffer_params.count_front_cache, buffer_params.count_qdisk, extra_msg_number)
    check_disk_buffer_metrics_after_sending_more_logs_than_buffer_can_handle(config, dqtool, expected_metrics_state.before)

    network_destination.start_listener()
    assert network_destination.read_until_logs([f"seq: {expected_metrics_state.last_msg_id}"])
    check_disk_buffer_metrics_after_destination_alive(config, dqtool, expected_metrics_state.after)

    syslog_ng.stop()
