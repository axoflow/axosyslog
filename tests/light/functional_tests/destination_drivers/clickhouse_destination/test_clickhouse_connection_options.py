#!/usr/bin/env python
#############################################################################
# Copyright (c) 2025 Axoflow
# Copyright (c) 2025 Andras Mitzki <andras.mitzki@axoflow.com>
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

import pytest
from axosyslog_light.driver_io.clickhouse.clickhouse_io import ClickhouseClient

clickhouse_options = {
    "database": "default",
    "table": "test_table",
    "user": "default",
    "password": "'password'",
    "schema": '"message" "String" => "$MSG"',
}


def test_clickhouse_destination_valid_options_db_run(request, config, syslog_ng, clickhouse_server):
    custom_input_msg = f"test message {str(uuid.uuid4())}"
    generator_source = config.create_example_msg_generator_source(num=1, template=f'"{custom_input_msg}"')
    clickhouse_destination = config.create_clickhouse_destination(**clickhouse_options)
    config.create_logpath(statements=[generator_source, clickhouse_destination])

    clickhouse_server.start()
    clickhouse_destination.create_table(clickhouse_options["table"], [("message", "String")])
    request.addfinalizer(lambda: clickhouse_destination.delete_table())

    syslog_ng.start(config)

    assert clickhouse_destination.get_stats() == {'msg_size_avg': 52, 'eps_last_1h': 0, 'processed': 1, 'eps_last_24h': 0, 'batch_size_max': 52, 'memory_usage': 0, 'dropped': 0, 'queued': 0, 'written': 1, 'eps_since_start': 0, 'msg_size_max': 52, 'batch_size_avg': 52}
    assert clickhouse_destination.read_logs() == [custom_input_msg]


def test_clickhouse_destination_valid_options_db_not_run_and_reconnect(request, config, syslog_ng, clickhouse_server):
    custom_input_msg = f"test message {str(uuid.uuid4())}"
    generator_source = config.create_example_msg_generator_source(num=1, template=f'"{custom_input_msg}"')
    clickhouse_options_copy = clickhouse_options.copy()
    clickhouse_options_copy.update({"time_reopen": 1})
    clickhouse_destination = config.create_clickhouse_destination(**clickhouse_options_copy)
    config.create_logpath(statements=[generator_source, clickhouse_destination])

    syslog_ng.start(config)

    assert clickhouse_destination.get_stats() == {'eps_last_24h': 0, 'processed': 1, 'eps_since_start': 0, 'memory_usage': 452, 'dropped': 0, 'queued': 1, 'written': 0, 'eps_last_1h': 0, 'msg_size_max': 52, 'msg_size_avg': 52, 'batch_size_max': 0, 'batch_size_avg': 0}

    assert syslog_ng.wait_for_message_in_console_log("Message added to ClickHouse batch") != []
    assert syslog_ng.wait_for_message_in_console_log("ClickHouse server responded with a temporary error status code") != []

    clickhouse_server.start()
    clickhouse_destination.create_table(clickhouse_options["table"], [("message", "String")])
    request.addfinalizer(lambda: clickhouse_destination.delete_table())

    syslog_ng.reload(config)  # Reload to connect to the ClickHouse server FIXME???

    assert clickhouse_destination.get_stats() == {'batch_size_max': 52, 'dropped': 0, 'queued': 0, 'written': 2, 'memory_usage': 0, 'eps_last_1h': 0, 'msg_size_max': 52, 'msg_size_avg': 52, 'batch_size_avg': 52, 'processed': 2, 'eps_since_start': 0, 'eps_last_24h': 0}
    assert clickhouse_destination.read_logs() == [custom_input_msg] * 2


@pytest.mark.parametrize(
    "ch_database, ch_table, ch_user, ch_password", [
        ("invalid_dababase", "test_table", "default", "'password'"),
        ("default", "invalid_table", "default", "'password'"),
        ("default", "test_table", "invalid_user", "'password'"),
        ("default", "test_table", "default", "'invalid_password'"),
    ], ids=["invalid_database", "invalid_table", "invalid_user", "invalid_password"],
)
def test_clickhouse_destination_invalid_options_db_run(request, config, syslog_ng, clickhouse_server, ch_database, ch_table, ch_user, ch_password):
    custom_input_msg = f"test message {str(uuid.uuid4())}"
    generator_source = config.create_example_msg_generator_source(num=1, template=f'"{custom_input_msg}"')
    clickhouse_destination = config.create_clickhouse_destination(database=ch_database, table=ch_table, user=ch_user, password=ch_password, schema='"message" "String" => "$MSG"')
    config.create_logpath(statements=[generator_source, clickhouse_destination])

    clickhouse_server.start()
    clickhouse_client = ClickhouseClient(username="default", password="password")
    clickhouse_client.create_table("test_table", [("message", "String")])
    request.addfinalizer(lambda: clickhouse_client.delete_table())

    syslog_ng.start(config)

    assert clickhouse_destination.get_stats()["written"] == 0

    assert syslog_ng.wait_for_message_in_console_log("Message added to ClickHouse batch") != []
    assert syslog_ng.wait_for_message_in_console_log("ClickHouse server responded with an exception") != []


@pytest.mark.parametrize(
    "ch_database, ch_table, ch_user", [
        (None, "test_table", "default"),
        ("default", None, "default"),
        ("default", "test_table", None),
    ], ids=["invalid_database", "invalid_table", "invalid_user"],
)
def test_clickhouse_destination_missing_mandatory_options(config, syslog_ng, clickhouse_server, ch_database, ch_table, ch_user):
    custom_input_msg = f"test message {str(uuid.uuid4())}"
    generator_source = config.create_example_msg_generator_source(num=1, template=f'"{custom_input_msg}"')
    clickhouse_options = {}
    if ch_database is not None:
        clickhouse_options.update({"database": ch_database})
    if ch_table is not None:
        clickhouse_options.update({"table": ch_table})
    if ch_user is not None:
        clickhouse_options.update({"user": ch_user})
    clickhouse_options.update({"schema": '"message" "String" => "$MSG"'})

    clickhouse_destination = config.create_clickhouse_destination(**clickhouse_options)
    config.create_logpath(statements=[generator_source, clickhouse_destination])

    with pytest.raises(Exception) as exec_info:
        syslog_ng.start(config)
    assert "syslog-ng is not running" in str(exec_info.value)
    assert syslog_ng.wait_for_message_in_console_log("Error initializing ClickHouse destination, database(), table() and user() are mandatory options") != []


def test_clickhouse_destination_valid_url_option_db_run(request, config, syslog_ng, clickhouse_server):
    custom_input_msg = f"test message {str(uuid.uuid4())}"
    generator_source = config.create_example_msg_generator_source(num=1, template=f'"{custom_input_msg}"')
    clickhouse_options_copy = clickhouse_options.copy()
    clickhouse_options_copy.update({"url": "'127.0.0.1:9100'"})
    clickhouse_destination = config.create_clickhouse_destination(**clickhouse_options_copy)
    config.create_logpath(statements=[generator_source, clickhouse_destination])

    clickhouse_server.start()
    clickhouse_destination.create_table(clickhouse_options_copy["table"], [("message", "String")])
    request.addfinalizer(lambda: clickhouse_destination.delete_table())

    syslog_ng.start(config)

    assert clickhouse_destination.get_stats()["written"] == 1
    assert clickhouse_destination.read_logs() == [custom_input_msg]


invalid_url_values = [
    ("'localhost'"),  # Missing port
    ("'invalid-domain:1234'"),  # Invalid domain
    ("'localhost:9100,localhost:9000'"),  # Multiple URLs not supported
    ("'@#@!#$RFSDSVCWRF SFsd'"),  # Garbage string
    ("' '"),  # Whitespace only
    ("'127.0.0.1'"),  # IPv4 address without port
    ("'::1'"),  # IPv6 address without port
]


@pytest.mark.parametrize("invalid_option_value", invalid_url_values, ids=range(len(invalid_url_values)))
def test_clickhouse_destination_invalid_url_option_db_run(request, config, syslog_ng, clickhouse_server, invalid_option_value):
    custom_input_msg = f"test message {str(uuid.uuid4())}"
    generator_source = config.create_example_msg_generator_source(num=1, template=f'"{custom_input_msg}"')
    clickhouse_options_copy = clickhouse_options.copy()
    clickhouse_options_copy.update({"url": invalid_option_value})
    clickhouse_destination = config.create_clickhouse_destination(**clickhouse_options_copy)
    config.create_logpath(statements=[generator_source, clickhouse_destination])

    clickhouse_server.start()
    clickhouse_destination.create_table("test_table", [("message", "String")])

    syslog_ng.start(config)

    assert clickhouse_destination.get_stats()["written"] == 0
