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
from axosyslog_light.common.blocking import wait_until_true
from axosyslog_light.driver_io.clickhouse.clickhouse_io import ClickhouseClient
from axosyslog_light.helpers.clickhouse.clickhouse_server_executor import CLICKHOUSE_OPTIONS_WITH_SCHEMA
from axosyslog_light.helpers.loggen.loggen import LoggenStartParams
from axosyslog_light.syslog_ng_config.__init__ import stringify


def test_clickhouse_destination_valid_options_db_run(request, config, syslog_ng, clickhouse_server, clickhouse_ports):
    custom_input_msg = f"test message {str(uuid.uuid4())}"
    generator_source = config.create_example_msg_generator_source(num=1, template=f'{stringify(custom_input_msg)}')
    clickhouse_options_copy = CLICKHOUSE_OPTIONS_WITH_SCHEMA.copy()
    clickhouse_options_copy.update({"url": f"'127.0.0.1:{clickhouse_ports.grpc_port}'"})
    clickhouse_destination = config.create_clickhouse_destination(**clickhouse_options_copy)
    clickhouse_destination.create_clickhouse_client(clickhouse_ports.http_port)
    config.create_logpath(statements=[generator_source, clickhouse_destination])

    clickhouse_server.start(clickhouse_ports)
    clickhouse_destination.create_table(CLICKHOUSE_OPTIONS_WITH_SCHEMA["table"], [("message", "String")])
    request.addfinalizer(lambda: clickhouse_destination.delete_table())

    syslog_ng.start(config)

    assert clickhouse_destination.get_stats()['written'] == 1
    assert clickhouse_destination.get_stats()['dropped'] == 0
    assert clickhouse_destination.get_stats()['queued'] == 0
    assert clickhouse_destination.read_logs() == [{"message": custom_input_msg}]


@pytest.mark.xfail(reason="known issue, see issue #747")
def test_clickhouse_destination_valid_options_db_not_run_and_reconnect(request, config, syslog_ng, clickhouse_server, clickhouse_ports):
    custom_input_msg = f"test message {str(uuid.uuid4())}"
    generator_source = config.create_example_msg_generator_source(num=1, template=f'{stringify(custom_input_msg)}')
    clickhouse_options_copy = CLICKHOUSE_OPTIONS_WITH_SCHEMA.copy()
    clickhouse_options_copy.update({"time_reopen": 1})
    clickhouse_options_copy.update({"url": f"'127.0.0.1:{clickhouse_ports.grpc_port}'"})
    clickhouse_destination = config.create_clickhouse_destination(**clickhouse_options_copy)
    clickhouse_destination.create_clickhouse_client(clickhouse_ports.http_port)
    config.create_logpath(statements=[generator_source, clickhouse_destination])

    syslog_ng.start(config)

    assert clickhouse_destination.get_stats()['written'] == 0
    assert clickhouse_destination.get_stats()['dropped'] == 0
    assert clickhouse_destination.get_stats()['queued'] == 1

    assert syslog_ng.wait_for_message_in_console_log("Message added to ClickHouse batch") != []
    assert syslog_ng.wait_for_message_in_console_log("ClickHouse server responded with a temporary error status code") != []

    clickhouse_server.start(clickhouse_ports)
    clickhouse_destination.create_table(CLICKHOUSE_OPTIONS_WITH_SCHEMA["table"], [("message", "String")])
    request.addfinalizer(lambda: clickhouse_destination.delete_table())

    assert clickhouse_destination.get_stats()['written'] == 2
    assert clickhouse_destination.get_stats()['dropped'] == 0
    assert clickhouse_destination.get_stats()['queued'] == 0
    assert clickhouse_destination.read_logs() == [{"message": custom_input_msg}] * 2


@pytest.mark.parametrize(
    "ch_database, ch_table, ch_user, ch_password", [
        # ("invalid_dababase", "test_table", "default", f'{stringify("password")}'),
        ("default", "invalid_table", "default", f'{stringify("password")}'),
        ("default", "test_table", "invalid_user", f'{stringify("password")}'),
        ("default", "test_table", "default", f'{stringify("invalid_password")}'),
    ], ids=["invalid_table", "invalid_user", "invalid_password"],
)
def test_clickhouse_destination_invalid_options_db_run(request, config, syslog_ng, clickhouse_server, ch_database, ch_table, ch_user, ch_password, clickhouse_ports):
    custom_input_msg = f"test message {str(uuid.uuid4())}"
    generator_source = config.create_example_msg_generator_source(num=1, template=f'{stringify(custom_input_msg)}')
    clickhouse_destination = config.create_clickhouse_destination(
        database=ch_database,
        table=ch_table,
        user=ch_user,
        password=ch_password,
        schema='"message" "String" => "$MSG"',
        url=f"'127.0.0.1:{clickhouse_ports.grpc_port}'",
    )
    clickhouse_destination.create_clickhouse_client(clickhouse_ports.http_port)
    config.create_logpath(statements=[generator_source, clickhouse_destination])

    clickhouse_server.start(clickhouse_ports)
    clickhouse_client = ClickhouseClient(username="default", password="password", http_port=clickhouse_ports.http_port)
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
    generator_source = config.create_example_msg_generator_source(num=1, template=f'{stringify(custom_input_msg)}')
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
def test_clickhouse_destination_invalid_url_option_db_run(request, config, syslog_ng, clickhouse_server, invalid_option_value, clickhouse_ports):
    custom_input_msg = f"test message {str(uuid.uuid4())}"
    generator_source = config.create_example_msg_generator_source(num=1, template=f'{stringify(custom_input_msg)}')
    clickhouse_options_copy = CLICKHOUSE_OPTIONS_WITH_SCHEMA.copy()
    clickhouse_options_copy.update({"url": invalid_option_value})
    clickhouse_destination = config.create_clickhouse_destination(**clickhouse_options_copy)
    clickhouse_destination.create_clickhouse_client(clickhouse_ports.http_port)
    config.create_logpath(statements=[generator_source, clickhouse_destination])

    clickhouse_server.start(clickhouse_ports)
    clickhouse_destination.create_table("test_table", [("message", "String")])

    syslog_ng.start(config)

    assert clickhouse_destination.get_stats()["written"] == 0


def test_clickhouse_destination_with_non_reliable_disk_buffer(request, config, syslog_ng, clickhouse_server, clickhouse_ports, port_allocator, loggen, dqtool):
    network_source = config.create_network_source(ip="localhost", port=port_allocator())
    clickhouse_options_copy = CLICKHOUSE_OPTIONS_WITH_SCHEMA.copy()
    clickhouse_options_copy.update({
        "url": f"'127.0.0.1:{clickhouse_ports.grpc_port}'",
        "disk-buffer": {"reliable": "no", "dir": "'.'", "capacity-bytes": "1MiB", "front-cache-size": 1000},
    })
    clickhouse_destination = config.create_clickhouse_destination(**clickhouse_options_copy)
    clickhouse_destination.create_clickhouse_client(clickhouse_ports.http_port)
    config.create_logpath(statements=[network_source, clickhouse_destination], flags="flow-control")
    syslog_ng.start(config)

    loggen_msg_number = 1400
    disk_buffer_max_size = 1324  # that number of messages can fit into the disk buffer with 1KiB message size
    loggen.start(LoggenStartParams(target=network_source.options["ip"], port=network_source.options["port"], inet=True, stream=True, rate=10000, size=1024, number=loggen_msg_number))
    wait_until_true(lambda: loggen.get_sent_message_count() == loggen_msg_number)

    syslog_ng.reload(config)

    syslog_ng.stop()

    dqtool_info_output = dqtool.info(disk_buffer_file='./syslog-ng-00000.qf')
    stderr_content = dqtool_info_output["stderr"]
    assert f"number_of_messages='{disk_buffer_max_size}'" in stderr_content, f"Expected {disk_buffer_max_size} messages in disk buffer, but got:\n{stderr_content}"

    clickhouse_server.start(clickhouse_ports)
    clickhouse_destination.create_table(CLICKHOUSE_OPTIONS_WITH_SCHEMA["table"], [("message", "String")])
    request.addfinalizer(lambda: clickhouse_destination.delete_table())

    syslog_ng.start(config)
    assert syslog_ng.wait_for_message_in_console_log("ClickHouse batch delivered") != []

    assert clickhouse_destination.get_stats()['written'] == disk_buffer_max_size
    assert clickhouse_destination.get_stats()['dropped'] == 0
    assert clickhouse_destination.get_stats()['queued'] == 0
