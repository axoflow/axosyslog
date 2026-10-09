#!/usr/bin/env python
#############################################################################
# Copyright (c) 2026 Axoflow
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
from axosyslog_light.common.blocking import wait_until_true
from helper_functions import get_metric
from helper_functions import loggen_send_messages

BATCH_LINES = 100
FLUSHED_BATCHES = 2
# the destination pops these into a partial batch: out of the queue, into the backlog, never flushed
LEFT_IN_BACKLOG = 50
FIRST_RUN = FLUSHED_BATCHES * BATCH_LINES + LEFT_IN_BACKLOG
# after the restart this completes the batch that starts with the replayed backlog
SECOND_RUN = BATCH_LINES - LEFT_IN_BACKLOG
# the partial batch must still be in the backlog when the test kills syslog-ng, so no timer may flush it
BATCH_TIMEOUT_MSEC = 3600 * 1000

DISK_BUFFER_FILE = "./syslog-ng-00000.rqf"


def seq(number):
    return f"seq: {number:010}"


def output_events(config, result):
    return get_metric(config, "syslogng_output_events_total", labels={"result": result})


def test_reliable_disk_buffer_replays_the_backlog_after_a_crash(config, syslog_ng, port_allocator, loggen):
    config.update_global_options(stats_level=2)
    network_source = config.create_network_source(ip="localhost", port=port_allocator())
    http_destination = config.create_http_destination(
        port=port_allocator(),
        body=config.stringify("${MSG}"),
        batch_lines=BATCH_LINES,
        batch_timeout=BATCH_TIMEOUT_MSEC,
        disk_buffer={"reliable": "yes", "dir": "'.'", "capacity-bytes": "1MiB"},
    )
    config.create_logpath(statements=[network_source, http_destination])

    syslog_ng.start(config)

    loggen_send_messages(loggen, network_source, number=FIRST_RUN)
    full_batches = http_destination.read_logs(FLUSHED_BATCHES)
    assert [len(body.splitlines()) for body in full_batches] == [BATCH_LINES] * FLUSHED_BATCHES

    # everything left the queue, the full batches were acked, the rest sits in the backlog
    assert wait_until_true(lambda: output_events(config, "delivered") == FLUSHED_BATCHES * BATCH_LINES)
    assert wait_until_true(lambda: output_events(config, "queued") == 0)

    syslog_ng.kill()

    syslog_ng.start(config)
    # the same file came back from the persist state and its header still knows about the backlog
    assert syslog_ng.wait_for_message_in_console_log(
        f"Reliable disk-buffer internal state; filename='{DISK_BUFFER_FILE}', queue_length='0', backlog_length='{LEFT_IN_BACKLOG}'",
    )

    loggen_send_messages(loggen, network_source, number=SECOND_RUN)
    body, = http_destination.read_logs(1)
    lines = body.splitlines()
    assert len(lines) == BATCH_LINES

    # the backlog is replayed first, in order, with nothing duplicated from the acked batches
    replayed, fresh = lines[:LEFT_IN_BACKLOG], lines[LEFT_IN_BACKLOG:]
    for i, line in enumerate(replayed):
        assert seq(FLUSHED_BATCHES * BATCH_LINES + i) in line
    # loggen numbers each run from 0
    for i, line in enumerate(fresh):
        assert seq(i) in line

    syslog_ng.stop()
    assert syslog_ng.is_message_in_console_log(
        f"Reliable disk-buffer state; operation='save', filename='{DISK_BUFFER_FILE}', number_of_messages='0'",
    )
