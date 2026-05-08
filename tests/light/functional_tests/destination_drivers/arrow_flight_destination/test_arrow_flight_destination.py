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
