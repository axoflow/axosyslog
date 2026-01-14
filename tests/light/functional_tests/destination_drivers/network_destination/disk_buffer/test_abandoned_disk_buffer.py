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

from helper_functions import check_abandoned_disk_buffer_metrics
from helper_functions import check_both_disk_buffer_metrics
from helper_functions import check_disk_buffer_files
from helper_functions import fill_up_and_check_initial_disk_buffer_metrics
from helper_functions import fill_up_and_check_new_disk_buffer_metrics
from helper_functions import set_config_with_default_non_reliable_disk_buffer_values
from helper_functions import trigger_to_create_abandoned_disk_buffer


NUMBER_OF_MESSAGES_TO_FILL_FIRST_BUFFERS = 500 + 724 + 100  # FrontCache + QDISK + WINDOW
NUMBER_OF_MESSAGES_TO_FILL_SECOND_BUFFERS = 1000 + 724 + 100  # FrontCache + QDISK + WINDOW

BufferState = namedtuple(
    "BufferState", [
        "syslogng_disk_queue_capacity_bytes",
        "syslogng_disk_queue_disk_allocated_bytes",
        "syslogng_disk_queue_disk_usage_bytes",
        "syslogng_disk_queue_events",
    ],
)

FIRST_DISK_BUFFER_METRICS = BufferState(
    syslogng_disk_queue_capacity_bytes=1044480,
    syslogng_disk_queue_disk_allocated_bytes=1048576,
    syslogng_disk_queue_disk_usage_bytes=1044480,
    syslogng_disk_queue_events=NUMBER_OF_MESSAGES_TO_FILL_FIRST_BUFFERS,
)
SECOND_DISK_BUFFER_METRICS = BufferState(
    syslogng_disk_queue_capacity_bytes=1044480,
    syslogng_disk_queue_disk_allocated_bytes=1048576,
    syslogng_disk_queue_disk_usage_bytes=1044480,
    syslogng_disk_queue_events=NUMBER_OF_MESSAGES_TO_FILL_SECOND_BUFFERS,
)
EMPTY_DISK_BUFFER_METRICS = BufferState(
    syslogng_disk_queue_capacity_bytes=1044480,
    syslogng_disk_queue_disk_allocated_bytes=1048576,
    syslogng_disk_queue_disk_usage_bytes=0,
    syslogng_disk_queue_events=0,
)


def test_abandoned_disk_buffer(config, port_allocator, syslog_ng, loggen, dqtool):
    config, network_source, network_destination = set_config_with_default_non_reliable_disk_buffer_values(config, port_allocator)

    syslog_ng.start(config)
    fill_up_and_check_initial_disk_buffer_metrics(config, loggen, None, network_source, NUMBER_OF_MESSAGES_TO_FILL_FIRST_BUFFERS, FIRST_DISK_BUFFER_METRICS)

    network_destination = trigger_to_create_abandoned_disk_buffer(network_destination, config, port_allocator, syslog_ng)

    fill_up_and_check_new_disk_buffer_metrics(config, loggen, network_source, NUMBER_OF_MESSAGES_TO_FILL_SECOND_BUFFERS, SECOND_DISK_BUFFER_METRICS)
    check_abandoned_disk_buffer_metrics(config, FIRST_DISK_BUFFER_METRICS)

    syslog_ng.stop()
    check_disk_buffer_files(dqtool, [("syslog-ng-00000.qf", NUMBER_OF_MESSAGES_TO_FILL_FIRST_BUFFERS), ("syslog-ng-00001.qf", NUMBER_OF_MESSAGES_TO_FILL_SECOND_BUFFERS)])

    network_destination.start_listener()
    syslog_ng.start(config)
    assert network_destination.read_until_logs([f"seq: 000000{NUMBER_OF_MESSAGES_TO_FILL_SECOND_BUFFERS - 1}"]), "Did not receive all messages from new disk buffer"

    check_both_disk_buffer_metrics(config, FIRST_DISK_BUFFER_METRICS, EMPTY_DISK_BUFFER_METRICS)
    check_disk_buffer_files(dqtool, [("syslog-ng-00000.qf", NUMBER_OF_MESSAGES_TO_FILL_FIRST_BUFFERS), ("syslog-ng-00001.qf", 0)])
