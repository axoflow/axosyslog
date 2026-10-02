#!/usr/bin/env python
#############################################################################
# Copyright (c) 2026 Axoflow
# Copyright (c) 2026 Attila Szakacs-Bertok <attila.szakacs@axoflow.com>
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
from axosyslog_light.common.blocking import wait_until_true
from axosyslog_light.syslog_ng_ctl.prometheus_stats_handler import MetricFilter
from helper_functions import buffer_params
from helper_functions import count_qdisk_for_capacity
from helper_functions import get_metric
from helper_functions import loggen_send_messages
from helper_functions import set_config_with_disk_buffer_values
from helper_functions import SIZE_OF_DISKQ_HEADER
from helper_functions import validate_disk_buffer
from helper_functions import validate_metrics

MiB = 1024 * 1024

# the destination pops while its server is down and a non-reliable pop refills the front cache from the file,
# so the non-reliable queue runs without a front cache to keep the counts on disk exact
FRONT_CACHE_SIZE = 100
# a non-reliable pop removes the record from the file, so the message the destination could not send lives
# in memory, a reliable one stays a record of the file until it is acked
HELD_BY_THE_DESTINATION = 1
OVERFILL = 10
# a partial drain frees this many records at the start of the file, the wrap pushes fewer behind the unread tail
DRAINED_BEFORE_WRAP = 200
PUSHED_BEHIND_THE_TAIL = 50
# records read before a check that the change is still pending, most of the tail stays unread
SOME_RECORDS = 100
# a drain serves one request per record, a 2 MiB file holds about 1500
DRAIN_TIMEOUT = 60

CAPACITY_CHANGED_MESSAGE = "Changed the capacity-bytes() of disk-buffer file"
CAPACITY_CHANGE_PENDING_MESSAGE = "capacity-bytes() changed, the new value will take effect once the disk-buffer content allows it"
FAILED_ATTEMPT_MESSAGE = "Server disconnected while preparing messages for sending"

BufferState = namedtuple(
    "BufferState", [
        "syslogng_disk_queue_capacity_bytes",
        "syslogng_disk_queue_disk_usage_bytes",
        "syslogng_disk_queue_events",
        "queued_syslogng_output_events_total",
        "dropped_syslogng_output_events_total",
        "messages_in_disk_buffer",
    ],
)


def useful_bytes(capacity_bytes):
    return capacity_bytes - SIZE_OF_DISKQ_HEADER


def round_down_to_kib(value):
    return value // 1024 * 1024


def front_cache_size(reliable):
    return FRONT_CACHE_SIZE if reliable else 0


def memory_count(reliable):
    return 0 if reliable else HELD_BY_THE_DESTINATION


def total_count(reliable, capacity_bytes):
    return memory_count(reliable) + count_qdisk_for_capacity(capacity_bytes)


def disk_buffer_file(reliable):
    return "./syslog-ng-00000.rqf" if reliable else "./syslog-ng-00000.qf"


def seq(number):
    return f"seq: {number:010}"


def full_state(reliable, capacity_bytes, dropped=0):
    return BufferState(
        syslogng_disk_queue_capacity_bytes=useful_bytes(capacity_bytes),
        syslogng_disk_queue_disk_usage_bytes=useful_bytes(capacity_bytes),
        syslogng_disk_queue_events=total_count(reliable, capacity_bytes),
        queued_syslogng_output_events_total=total_count(reliable, capacity_bytes),
        dropped_syslogng_output_events_total=dropped,
        messages_in_disk_buffer=count_qdisk_for_capacity(capacity_bytes),
    )


def empty_state(capacity_bytes, dropped=0):
    return BufferState(
        syslogng_disk_queue_capacity_bytes=useful_bytes(capacity_bytes),
        syslogng_disk_queue_disk_usage_bytes=0,
        syslogng_disk_queue_events=0,
        queued_syslogng_output_events_total=0,
        dropped_syslogng_output_events_total=dropped,
        messages_in_disk_buffer=0,
    )


def validate_buffer(config, dqtool, expected, reliable):
    validate_metrics(config, expected, disk_buffer_file=disk_buffer_file(reliable))
    validate_disk_buffer(dqtool, expected.messages_in_disk_buffer, disk_buffer_file=disk_buffer_file(reliable))


def validate_metric(config, reliable, metric_name, expected_value):
    assert wait_until_true(lambda: get_metric(config, metric_name, labels={"path": disk_buffer_file(reliable)}) == expected_value)


def validate_full(config, dqtool, reliable, capacity_bytes, dropped=0):
    validate_buffer(config, dqtool, full_state(reliable, capacity_bytes, dropped), reliable)
    # the disk metrics have a granularity of 1 KiB, rounded down
    allocated_bytes = round_down_to_kib(SIZE_OF_DISKQ_HEADER + count_qdisk_for_capacity(capacity_bytes) * buffer_params.message_size_in_diskq)
    validate_metric(config, reliable, "syslogng_disk_queue_disk_allocated_bytes", allocated_bytes)


def read_some_and_validate_pending_capacity(config, http_destination, reliable, capacity_bytes, unread_bytes_at_least):
    """Serve a few requests, then check that the file still holds unread records beyond the given amount and
    that the capacity did not change."""
    http_destination.start_listener()
    http_destination.read_logs(SOME_RECORDS)
    http_destination.stop_listener()
    path = disk_buffer_file(reliable)
    samples = config.get_prometheus_samples([
        MetricFilter("syslogng_disk_queue_disk_usage_bytes", {"path": path}),
        MetricFilter("syslogng_disk_queue_capacity_bytes", {"path": path}),
    ])
    values = {sample.name: sample.value for sample in samples}
    assert values["syslogng_disk_queue_disk_usage_bytes"] > unread_bytes_at_least, "the file drained before the check"
    assert values["syslogng_disk_queue_capacity_bytes"] == useful_bytes(capacity_bytes)


def fill(syslog_ng, loggen, network_source, number):
    """Send one message and wait for the first failed attempt of the destination before the rest.

    The destination pops the message as soon as it arrives and keeps it after the failed attempt, so the later
    attempts repeat with that message and leave the file alone. A non-reliable pop of the only record empties
    the file and resets it, so the rest starts on a pristine file. The first message is a run of its own, so it
    carries seq 0 and the rest start from 0 again.
    """
    attempts = syslog_ng.count_message_in_console_log(FAILED_ATTEMPT_MESSAGE)
    loggen_send_messages(loggen, network_source, number=1)
    assert wait_until_true(lambda: syslog_ng.count_message_in_console_log(FAILED_ATTEMPT_MESSAGE) > attempts)
    loggen_send_messages(loggen, network_source, number=number - 1)


def drain(http_destination, last_seqs):
    """Serve requests until the last message of each loggen run arrived, then stop the server. Every run numbers
    its messages from 0 and the runs arrive in order. The request in flight at the stop is still served and
    stays readable, so at most one more record leaves the file."""
    http_destination.start_listener()
    for last_seq in last_seqs:
        assert http_destination.read_until_logs([seq(last_seq)], timeout=DRAIN_TIMEOUT)
    http_destination.stop_listener()


def wrap_disk_buffer(syslog_ng, config, dqtool, loggen, network_source, http_destination, reliable, capacity_bytes, dropped):
    """Drain the start of a full file, then push behind its unread tail: the write head wraps to the start.

    The pushed run is shorter than the drained part, so its seq numbers stay below the ones of
    the tail and the drain can wait for its last message.
    """
    fill(syslog_ng, loggen, network_source, total_count(reliable, capacity_bytes))
    validate_full(config, dqtool, reliable, capacity_bytes, dropped)

    drain(http_destination, [0, DRAINED_BEFORE_WRAP - 2])
    loggen_send_messages(loggen, network_source, number=PUSHED_BEHIND_THE_TAIL)
    # the pushed messages found room at the start of the file
    validate_metric(config, reliable, "syslogng_disk_queue_capacity_bytes", useful_bytes(capacity_bytes))
    assert wait_until_true(lambda: get_metric(config, "syslogng_output_events_total", labels={"result": "dropped"}) == dropped)


def check_disk_buffer_is_usable_at_capacity(syslog_ng, config, dqtool, loggen, network_source, http_destination, reliable, capacity_bytes):
    """Nothing may have been dropped before."""
    fill(syslog_ng, loggen, network_source, total_count(reliable, capacity_bytes))
    validate_full(config, dqtool, reliable, capacity_bytes)
    drain(http_destination, [0, total_count(reliable, capacity_bytes) - 2])
    validate_buffer(config, dqtool, empty_state(capacity_bytes), reliable)

    fill(syslog_ng, loggen, network_source, total_count(reliable, capacity_bytes) + OVERFILL)
    validate_full(config, dqtool, reliable, capacity_bytes, dropped=OVERFILL)
    drain(http_destination, [0, total_count(reliable, capacity_bytes) - 2])
    validate_buffer(config, dqtool, empty_state(capacity_bytes, dropped=OVERFILL), reliable)

    wrap_disk_buffer(syslog_ng, config, dqtool, loggen, network_source, http_destination, reliable, capacity_bytes, dropped=OVERFILL)
    drain(http_destination, [total_count(reliable, capacity_bytes) - 2, PUSHED_BEHIND_THE_TAIL - 1])
    validate_buffer(config, dqtool, empty_state(capacity_bytes, dropped=OVERFILL), reliable)


def start_without_trace(syslog_ng, config):
    """The trace lines of thousands of http() requests make the console log too long for the stop message
    to be found in time."""
    syslog_ng.start_params.trace = False
    syslog_ng.start(config)


def change_capacity(syslog_ng, config, http_destination, capacity_bytes, change_config):
    http_destination.options["disk_buffer"]["capacity-bytes"] = str(capacity_bytes)
    if change_config == "reload":
        syslog_ng.reload(config)
    else:
        syslog_ng.stop()
        syslog_ng.start(config)


@pytest.mark.parametrize("change_config", ["reload", "restart"])
@pytest.mark.parametrize("reliable", [True, False], ids=["reliable", "non-reliable"])
def test_grow_applies_at_config_change(config, port_allocator, syslog_ng, loggen, dqtool, reliable, change_config):
    config, network_source, http_destination = set_config_with_disk_buffer_values(config, port_allocator, reliable, 1 * MiB, front_cache_size(reliable))

    start_without_trace(syslog_ng, config)
    fill(syslog_ng, loggen, network_source, total_count(reliable, 1 * MiB))
    validate_full(config, dqtool, reliable, 1 * MiB)
    validate_metric(config, reliable, "syslogng_disk_queue_processed_events_total", total_count(reliable, 1 * MiB))

    change_capacity(syslog_ng, config, http_destination, 2 * MiB, change_config)
    validate_buffer(
        config, dqtool, BufferState(
            syslogng_disk_queue_capacity_bytes=useful_bytes(2 * MiB),
            syslogng_disk_queue_disk_usage_bytes=round_down_to_kib(count_qdisk_for_capacity(1 * MiB) * buffer_params.message_size_in_diskq),
            syslogng_disk_queue_events=total_count(reliable, 1 * MiB),
            queued_syslogng_output_events_total=total_count(reliable, 1 * MiB),
            dropped_syslogng_output_events_total=0,
            messages_in_disk_buffer=count_qdisk_for_capacity(1 * MiB),
        ), reliable,
    )
    assert syslog_ng.is_message_in_console_log(CAPACITY_CHANGED_MESSAGE)
    assert not syslog_ng.is_message_in_console_log(CAPACITY_CHANGE_PENDING_MESSAGE)

    # the messages of the first run are still queued, the second run fills the rest up to the new capacity
    loggen_send_messages(loggen, network_source, number=count_qdisk_for_capacity(2 * MiB) - count_qdisk_for_capacity(1 * MiB))
    validate_full(config, dqtool, reliable, 2 * MiB)

    drain(http_destination, [0, total_count(reliable, 1 * MiB) - 2, count_qdisk_for_capacity(2 * MiB) - count_qdisk_for_capacity(1 * MiB) - 1])
    validate_buffer(config, dqtool, empty_state(2 * MiB), reliable)
    assert wait_until_true(lambda: get_metric(config, "syslogng_output_events_total", labels={"result": "delivered"}) == total_count(reliable, 2 * MiB))

    check_disk_buffer_is_usable_at_capacity(syslog_ng, config, dqtool, loggen, network_source, http_destination, reliable, 2 * MiB)
    syslog_ng.stop()


@pytest.mark.parametrize("change_config", ["reload", "restart"])
@pytest.mark.parametrize("reliable", [True, False], ids=["reliable", "non-reliable"])
def test_grow_cannot_apply_while_wrapped_then_applies_at_unwrap(config, port_allocator, syslog_ng, loggen, dqtool, reliable, change_config):
    config, network_source, http_destination = set_config_with_disk_buffer_values(config, port_allocator, reliable, 1 * MiB, front_cache_size(reliable))

    start_without_trace(syslog_ng, config)
    wrap_disk_buffer(syslog_ng, config, dqtool, loggen, network_source, http_destination, reliable, 1 * MiB, dropped=0)

    change_capacity(syslog_ng, config, http_destination, 2 * MiB, change_config)
    validate_metric(config, reliable, "syslogng_disk_queue_capacity_bytes", useful_bytes(1 * MiB))
    assert syslog_ng.count_message_in_console_log(CAPACITY_CHANGE_PENDING_MESSAGE) == 1
    assert not syslog_ng.is_message_in_console_log(CAPACITY_CHANGED_MESSAGE)

    # the acks of the tail cannot apply the change while the file stays wrapped
    read_some_and_validate_pending_capacity(config, http_destination, reliable, 1 * MiB, (PUSHED_BEHIND_THE_TAIL + 1) * buffer_params.message_size_in_diskq)

    # the change takes effect while the tail and the pushed records drain, the acks do not repeat the warning
    drain(http_destination, [total_count(reliable, 1 * MiB) - 2, PUSHED_BEHIND_THE_TAIL - 1])
    validate_buffer(config, dqtool, empty_state(2 * MiB), reliable)
    assert syslog_ng.is_message_in_console_log(CAPACITY_CHANGED_MESSAGE)
    assert syslog_ng.count_message_in_console_log(CAPACITY_CHANGE_PENDING_MESSAGE) == 1

    check_disk_buffer_is_usable_at_capacity(syslog_ng, config, dqtool, loggen, network_source, http_destination, reliable, 2 * MiB)
    syslog_ng.stop()


@pytest.mark.parametrize("change_config", ["reload", "restart"])
@pytest.mark.parametrize("reliable", [True, False], ids=["reliable", "non-reliable"])
def test_shrink_cannot_apply_while_content_does_not_fit_then_applies_at_reset(config, port_allocator, syslog_ng, loggen, dqtool, reliable, change_config):
    config, network_source, http_destination = set_config_with_disk_buffer_values(config, port_allocator, reliable, 2 * MiB, front_cache_size(reliable))

    start_without_trace(syslog_ng, config)
    fill(syslog_ng, loggen, network_source, total_count(reliable, 2 * MiB))
    validate_full(config, dqtool, reliable, 2 * MiB)
    validate_metric(config, reliable, "syslogng_disk_queue_processed_events_total", total_count(reliable, 2 * MiB))

    change_capacity(syslog_ng, config, http_destination, 1 * MiB, change_config)
    validate_buffer(config, dqtool, full_state(reliable, 2 * MiB), reliable)
    assert syslog_ng.count_message_in_console_log(CAPACITY_CHANGE_PENDING_MESSAGE) == 1
    assert not syslog_ng.is_message_in_console_log(CAPACITY_CHANGED_MESSAGE)

    # the acks cannot apply the change while the write head is beyond the new boundary
    read_some_and_validate_pending_capacity(config, http_destination, reliable, 2 * MiB, 0)

    # the empty-file reset moves the write head back, then the change takes effect and the file is truncated
    drain(http_destination, [total_count(reliable, 2 * MiB) - 2])
    validate_buffer(config, dqtool, empty_state(1 * MiB), reliable)
    validate_metric(config, reliable, "syslogng_disk_queue_disk_allocated_bytes", 1 * MiB)
    assert wait_until_true(lambda: get_metric(config, "syslogng_output_events_total", labels={"result": "delivered"}) == total_count(reliable, 2 * MiB))
    assert syslog_ng.is_message_in_console_log(CAPACITY_CHANGED_MESSAGE)
    assert syslog_ng.count_message_in_console_log(CAPACITY_CHANGE_PENDING_MESSAGE) == 1

    check_disk_buffer_is_usable_at_capacity(syslog_ng, config, dqtool, loggen, network_source, http_destination, reliable, 1 * MiB)
    syslog_ng.stop()


@pytest.mark.parametrize("change_config", ["reload", "restart"])
@pytest.mark.parametrize("reliable", [True, False], ids=["reliable", "non-reliable"])
def test_shrink_applies_at_config_change(config, port_allocator, syslog_ng, loggen, dqtool, reliable, change_config):
    config, network_source, http_destination = set_config_with_disk_buffer_values(config, port_allocator, reliable, 2 * MiB, front_cache_size(reliable))
    queued_count = 200
    partial_state = BufferState(
        syslogng_disk_queue_capacity_bytes=useful_bytes(2 * MiB),
        syslogng_disk_queue_disk_usage_bytes=round_down_to_kib(queued_count * buffer_params.message_size_in_diskq),
        syslogng_disk_queue_events=memory_count(reliable) + queued_count,
        queued_syslogng_output_events_total=memory_count(reliable) + queued_count,
        dropped_syslogng_output_events_total=0,
        messages_in_disk_buffer=queued_count,
    )

    start_without_trace(syslog_ng, config)
    fill(syslog_ng, loggen, network_source, memory_count(reliable) + queued_count)
    validate_buffer(config, dqtool, partial_state, reliable)

    # the content fits into the new capacity, the change takes effect at once
    change_capacity(syslog_ng, config, http_destination, 1 * MiB, change_config)
    validate_buffer(config, dqtool, partial_state._replace(syslogng_disk_queue_capacity_bytes=useful_bytes(1 * MiB)), reliable)
    assert syslog_ng.is_message_in_console_log(CAPACITY_CHANGED_MESSAGE)
    assert not syslog_ng.is_message_in_console_log(CAPACITY_CHANGE_PENDING_MESSAGE)

    drain(http_destination, [0, memory_count(reliable) + queued_count - 2])
    validate_buffer(config, dqtool, empty_state(1 * MiB), reliable)

    check_disk_buffer_is_usable_at_capacity(syslog_ng, config, dqtool, loggen, network_source, http_destination, reliable, 1 * MiB)
    syslog_ng.stop()


@pytest.mark.parametrize("change_config", ["reload", "restart"])
@pytest.mark.parametrize("reliable", [True, False], ids=["reliable", "non-reliable"])
def test_shrink_cannot_apply_while_wrapped_then_applies_at_unwrap(config, port_allocator, syslog_ng, loggen, dqtool, reliable, change_config):
    config, network_source, http_destination = set_config_with_disk_buffer_values(config, port_allocator, reliable, 2 * MiB, front_cache_size(reliable))

    start_without_trace(syslog_ng, config)
    # the unread tail reaches beyond the new boundary, the records pushed behind it sit at the start of the file
    wrap_disk_buffer(syslog_ng, config, dqtool, loggen, network_source, http_destination, reliable, 2 * MiB, dropped=0)

    change_capacity(syslog_ng, config, http_destination, 1 * MiB, change_config)
    validate_metric(config, reliable, "syslogng_disk_queue_capacity_bytes", useful_bytes(2 * MiB))
    assert syslog_ng.count_message_in_console_log(CAPACITY_CHANGE_PENDING_MESSAGE) == 1
    assert not syslog_ng.is_message_in_console_log(CAPACITY_CHANGED_MESSAGE)

    # the acks of the tail cannot apply the change while the file stays wrapped
    read_some_and_validate_pending_capacity(config, http_destination, reliable, 2 * MiB, (PUSHED_BEHIND_THE_TAIL + 1) * buffer_params.message_size_in_diskq)

    # the write head sits within the new boundary once the tail is acked, so the change takes effect while
    # the pushed records drain
    drain(http_destination, [total_count(reliable, 2 * MiB) - 2, PUSHED_BEHIND_THE_TAIL - 1])
    validate_buffer(config, dqtool, empty_state(1 * MiB), reliable)
    validate_metric(config, reliable, "syslogng_disk_queue_disk_allocated_bytes", 1 * MiB)
    assert syslog_ng.is_message_in_console_log(CAPACITY_CHANGED_MESSAGE)
    assert syslog_ng.count_message_in_console_log(CAPACITY_CHANGE_PENDING_MESSAGE) == 1

    check_disk_buffer_is_usable_at_capacity(syslog_ng, config, dqtool, loggen, network_source, http_destination, reliable, 1 * MiB)
    syslog_ng.stop()
