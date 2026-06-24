#!/usr/bin/env python
#############################################################################
# Copyright (c) 2026 Axoflow
# Copyright (c) 2026 Attila Szakacs <attila.szakacs@axoflow.com>
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
import uuid

from axosyslog_light.common.blocking import wait_until_true
from axosyslog_light.syslog_ng_config.__init__ import stringify

BIGQUERY_OPTIONS = {
    "schema": '"message" STRING => $MSG',
}


def test_bigquery_destination_single_message(config, syslog_ng, port_allocator):
    custom_msg = f"test message {uuid.uuid4()}"
    generator_source = config.create_example_msg_generator_source(num=1, template=stringify(custom_msg))

    bigquery_destination = config.create_bigquery_destination(port=port_allocator(), **BIGQUERY_OPTIONS)
    config.create_logpath(statements=[generator_source, bigquery_destination])

    bigquery_destination.start_listener()
    syslog_ng.start(config)

    assert wait_until_true(lambda: bigquery_destination.get_stats().get("written", 0) == 1)
    assert bigquery_destination.get_stats()["dropped"] == 0
    assert bigquery_destination.get_row_count() == 1
    assert bigquery_destination.read_logs() == [{"message": custom_msg}]
    assert bigquery_destination.get_stream_count() == 1
    assert bigquery_destination.get_create_count() == 1


def test_bigquery_destination_multiple_messages(config, syslog_ng, port_allocator):
    num_messages = 10
    custom_msg = f"test message {uuid.uuid4()}"
    generator_source = config.create_example_msg_generator_source(num=num_messages, template=stringify(custom_msg))

    options = {
        **BIGQUERY_OPTIONS,
        "batch-lines": num_messages,
        "batch-timeout": 10000,
    }
    bigquery_destination = config.create_bigquery_destination(port=port_allocator(), **options)
    config.create_logpath(statements=[generator_source, bigquery_destination])

    bigquery_destination.start_listener()
    syslog_ng.start(config)

    assert wait_until_true(lambda: bigquery_destination.get_stats().get("written", 0) == num_messages)
    assert bigquery_destination.get_stats()["dropped"] == 0
    assert bigquery_destination.get_row_count() == num_messages
    assert bigquery_destination.read_logs() == [{"message": custom_msg}] * num_messages
    assert bigquery_destination.get_batch_count() == 1
    assert bigquery_destination.get_stream_count() == 1


def test_bigquery_destination_server_not_running(config, syslog_ng, port_allocator):
    custom_msg = f"test message {uuid.uuid4()}"
    generator_source = config.create_example_msg_generator_source(num=1, template=stringify(custom_msg))

    bigquery_destination = config.create_bigquery_destination(port=port_allocator(), **BIGQUERY_OPTIONS)
    config.create_logpath(statements=[generator_source, bigquery_destination])

    syslog_ng.start(config)

    assert wait_until_true(lambda: bigquery_destination.get_stats().get("queued", 0) == 1)
    assert bigquery_destination.get_stats()["written"] == 0

    bigquery_destination.start_listener()

    assert wait_until_true(lambda: bigquery_destination.get_stats().get("written", 0) == 1)
    assert bigquery_destination.get_row_count() == 1
    assert bigquery_destination.read_logs() == [{"message": custom_msg}]


def test_bigquery_destination_recovers_after_server_stream_restart(config, syslog_ng, port_allocator):
    # Regression test for #957: the BigQuery Storage Write API restarts the
    # AppendRows stream server-side. The in-flight Write()/Read() then fails and
    # the gRPC call reaches a terminal state; the old disconnect() ran the
    # WritesDone()/Finish() half-close on that completed call, which made gRPC
    # core abort the process with GRPC_CALL_ERROR_TOO_MANY_OPERATIONS (exit 139).
    # The worker must instead survive the restart and keep delivering by
    # re-establishing the write stream on reconnect.
    custom_msg = f"test message {uuid.uuid4()}"
    file_source = config.create_file_source(file_name="input.log", flags="no-parse")

    options = {
        **BIGQUERY_OPTIONS,
        "batch-lines": 1,
        "time-reopen": 1,
    }
    bigquery_destination = config.create_bigquery_destination(port=port_allocator(), **options)
    config.create_logpath(statements=[file_source, bigquery_destination])

    bigquery_destination.start_listener()
    bigquery_destination.restart_stream_after_next_batch()
    syslog_ng.start(config)

    # The first batch is acked, then the server tears the stream down.
    file_source.write_log(custom_msg)
    assert wait_until_true(lambda: bigquery_destination.get_stats().get("written", 0) == 1)

    # The stream restart must not crash syslog-ng.
    assert syslog_ng.is_process_running()

    # A second message must be delivered on a freshly re-established stream.
    file_source.write_log(custom_msg)
    assert wait_until_true(lambda: bigquery_destination.get_stats().get("written", 0) == 2)
    assert bigquery_destination.get_stats()["dropped"] == 0
    assert bigquery_destination.get_row_count() == 2
    assert bigquery_destination.read_logs() == [{"message": custom_msg}, {"message": custom_msg}]

    # Recovery re-created the write stream and opened a new AppendRows call.
    assert bigquery_destination.get_create_count() >= 2
    assert bigquery_destination.get_stream_count() >= 2
    assert syslog_ng.is_process_running()
