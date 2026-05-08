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
import datetime
import uuid

from axosyslog_light.common.blocking import wait_until_true
from axosyslog_light.syslog_ng_config.__init__ import stringify

ARROW_FLIGHT_PATH = "test-path"

ARROW_FLIGHT_OPTIONS = {
    "path": stringify(ARROW_FLIGHT_PATH),
    "schema": '"message" STRING => $MSG',
}


def test_arrow_flight_destination_single_message(config, syslog_ng, port_allocator):
    custom_msg = f"test message {uuid.uuid4()}"
    generator_source = config.create_example_msg_generator_source(num=1, template=stringify(custom_msg))

    arrow_flight_destination = config.create_arrow_flight_destination(port=port_allocator(), **ARROW_FLIGHT_OPTIONS)
    config.create_logpath(statements=[generator_source, arrow_flight_destination])

    arrow_flight_destination.start_listener()
    syslog_ng.start(config)

    assert wait_until_true(lambda: arrow_flight_destination.get_stats().get("written", 0) == 1)
    assert arrow_flight_destination.get_stats()["dropped"] == 0
    assert arrow_flight_destination.read_logs(ARROW_FLIGHT_PATH) == [{"message": custom_msg}]
    assert arrow_flight_destination.get_stream_count(ARROW_FLIGHT_PATH) == 1
    assert arrow_flight_destination.get_batch_count(ARROW_FLIGHT_PATH) == 1


def test_arrow_flight_destination_multiple_messages(config, syslog_ng, port_allocator):
    num_messages = 10
    custom_msg = f"test message {uuid.uuid4()}"
    generator_source = config.create_example_msg_generator_source(num=num_messages, template=stringify(custom_msg))

    options = {
        **ARROW_FLIGHT_OPTIONS,
        "batch-lines": num_messages,
        "batch-timeout": 10000,
    }
    arrow_flight_destination = config.create_arrow_flight_destination(port=port_allocator(), **options)
    config.create_logpath(statements=[generator_source, arrow_flight_destination])

    arrow_flight_destination.start_listener()
    syslog_ng.start(config)

    assert wait_until_true(lambda: arrow_flight_destination.get_stats().get("written", 0) == num_messages)
    assert arrow_flight_destination.get_stats()["dropped"] == 0
    assert arrow_flight_destination.get_row_count(ARROW_FLIGHT_PATH) == num_messages
    assert arrow_flight_destination.get_stream_count(ARROW_FLIGHT_PATH) == 1
    assert arrow_flight_destination.get_batch_count(ARROW_FLIGHT_PATH) == 1


def test_arrow_flight_destination_batching(config, syslog_ng, port_allocator):
    num_messages = 9
    batch_size = 3
    custom_msg = f"test message {uuid.uuid4()}"
    generator_source = config.create_example_msg_generator_source(num=num_messages, template=stringify(custom_msg))

    options = {
        **ARROW_FLIGHT_OPTIONS,
        "batch-lines": batch_size,
        "batch-timeout": 10000,
    }
    arrow_flight_destination = config.create_arrow_flight_destination(port=port_allocator(), **options)
    config.create_logpath(statements=[generator_source, arrow_flight_destination])

    arrow_flight_destination.start_listener()
    syslog_ng.start(config)

    assert wait_until_true(lambda: arrow_flight_destination.get_stats().get("written", 0) == num_messages)
    assert arrow_flight_destination.get_stats()["dropped"] == 0
    assert arrow_flight_destination.get_row_count(ARROW_FLIGHT_PATH) == num_messages
    assert arrow_flight_destination.get_stream_count(ARROW_FLIGHT_PATH) == 1
    assert arrow_flight_destination.get_batch_count(ARROW_FLIGHT_PATH) == num_messages // batch_size


def test_arrow_flight_destination_multiple_schema_fields(config, syslog_ng, port_allocator):
    custom_msg = f"test message {uuid.uuid4()}"
    generator_source = config.create_example_msg_generator_source(num=1, template=stringify(custom_msg))

    options = {
        "path": stringify(ARROW_FLIGHT_PATH),
        "schema": (
            '"message" STRING => $MSG '
            '"count" INT64 => "42" '
            '"flag" BOOL => "true" '
            '"ts" TIMESTAMP => "1700000000000000000"'
        ),
        "batch-lines": 1,
    }
    arrow_flight_destination = config.create_arrow_flight_destination(port=port_allocator(), **options)

    config.create_logpath(statements=[generator_source, arrow_flight_destination])

    arrow_flight_destination.start_listener()
    syslog_ng.start(config)

    assert wait_until_true(lambda: arrow_flight_destination.get_stats().get("written", 0) == 1)
    rows = arrow_flight_destination.read_logs(ARROW_FLIGHT_PATH)
    assert len(rows) == 1
    assert rows[0]["message"] == custom_msg
    assert rows[0]["count"] == 42
    assert rows[0]["flag"] is True
    assert rows[0]["ts"] == datetime.datetime(2023, 11, 14, 22, 13, 20, tzinfo=datetime.timezone.utc)
    assert arrow_flight_destination.get_stream_count(ARROW_FLIGHT_PATH) == 1
    assert arrow_flight_destination.get_batch_count(ARROW_FLIGHT_PATH) == 1


def test_arrow_flight_destination_templated_path(config, syslog_ng, port_allocator):
    msg_a = f"message-a {uuid.uuid4()}"
    msg_b = f"message-b {uuid.uuid4()}"

    generator_a = config.create_example_msg_generator_source(num=1, template=stringify(msg_a))
    generator_b = config.create_example_msg_generator_source(num=1, template=stringify(msg_b))

    rewrite_a = config.create_rewrite_set("'path-a'", value="DEST_PATH")
    rewrite_b = config.create_rewrite_set("'path-b'", value="DEST_PATH")

    options = {
        "path": '"${DEST_PATH}"',
        "schema": '"message" STRING => $MSG',
        "batch-lines": 1,
        "worker-partition-key": '"${DEST_PATH}"',
    }
    dest = config.create_arrow_flight_destination(port=port_allocator(), **options)
    config.create_logpath(statements=[generator_a, rewrite_a, dest])
    config.create_logpath(statements=[generator_b, rewrite_b, dest])

    dest.start_listener()
    syslog_ng.start(config)

    assert wait_until_true(lambda: dest.get_stats().get("written", 0) == 2)
    assert dest.get_stats()["dropped"] == 0
    assert dest.read_logs("path-a") == [{"message": msg_a}]
    assert dest.read_logs("path-b") == [{"message": msg_b}]
    assert dest.get_stream_count("path-a") == 1
    assert dest.get_stream_count("path-b") == 1
    assert dest.get_batch_count("path-a") == 1
    assert dest.get_batch_count("path-b") == 1


def test_arrow_flight_destination_templated_path_flush_on_key_change(config, syslog_ng, port_allocator):
    path_a = "path-a"
    path_b = "path-b"
    batch_lines = 4
    small_burst = 3
    bursts = [(path_a, small_burst), (path_b, small_burst), (path_a, small_burst), (path_b, batch_lines)]
    total_messages = sum(n for _, n in bursts)

    file_source = config.create_file_source(file_name="input.log", flags="no-parse")
    options = {
        "path": '"${MSG}"',
        "schema": '"key" STRING => $MSG',
        "batch-lines": batch_lines,
        "batch-timeout": 10000,
        "worker-partition-key": '"${MSG}"',
    }
    dest = config.create_arrow_flight_destination(port=port_allocator(), **options)
    config.create_logpath(statements=[file_source, dest])

    dest.start_listener()
    syslog_ng.start(config)

    for path, n in bursts:
        file_source.write_logs([path] * n)

    assert wait_until_true(lambda: dest.get_stats().get("written", 0) == total_messages)
    assert dest.get_stats()["dropped"] == 0
    assert dest.get_row_count(path_a) == small_burst * 2
    assert dest.get_row_count(path_b) == small_burst + batch_lines
    assert dest.get_batch_count(path_a) == 2
    assert dest.get_batch_count(path_b) == 2
    assert dest.get_stream_count(path_a) == 2
    assert dest.get_stream_count(path_b) == 2


def test_arrow_flight_destination_reconnect(config, syslog_ng, port_allocator):
    custom_msg = f"test message {uuid.uuid4()}"

    file_source = config.create_file_source(file_name="input.log", flags="no-parse")
    options = {
        **ARROW_FLIGHT_OPTIONS,
        "time-reopen": 1,
    }
    arrow_flight_destination = config.create_arrow_flight_destination(port=port_allocator(), **options)
    config.create_logpath(statements=[file_source, arrow_flight_destination])

    arrow_flight_destination.start_listener()
    arrow_flight_destination.close_stream_after_next_request()
    syslog_ng.start(config)

    file_source.write_log(custom_msg)
    assert wait_until_true(lambda: arrow_flight_destination.get_stats().get("written", 0) == 1)

    arrow_flight_destination.stop_listener()

    file_source.write_log(custom_msg)

    arrow_flight_destination.start_listener()
    assert wait_until_true(lambda: arrow_flight_destination.get_stats().get("written", 0) == 2)
    assert arrow_flight_destination.get_stats()["dropped"] == 0


def test_arrow_flight_destination_server_not_running(config, syslog_ng, port_allocator):
    custom_msg = f"test message {uuid.uuid4()}"
    generator_source = config.create_example_msg_generator_source(num=1, template=stringify(custom_msg))

    arrow_flight_destination = config.create_arrow_flight_destination(port=port_allocator(), **ARROW_FLIGHT_OPTIONS)
    config.create_logpath(statements=[generator_source, arrow_flight_destination])

    syslog_ng.start(config)

    assert wait_until_true(lambda: arrow_flight_destination.get_stats().get("queued", 0) == 1)
    assert arrow_flight_destination.get_stats()["written"] == 0
    assert arrow_flight_destination.get_stats()["dropped"] == 0

    arrow_flight_destination.start_listener()

    assert wait_until_true(lambda: arrow_flight_destination.get_stats().get("written", 0) == 1)
    assert arrow_flight_destination.read_logs(ARROW_FLIGHT_PATH) == [{"message": custom_msg}]
    assert arrow_flight_destination.get_stream_count(ARROW_FLIGHT_PATH) == 1
    assert arrow_flight_destination.get_batch_count(ARROW_FLIGHT_PATH) == 1
