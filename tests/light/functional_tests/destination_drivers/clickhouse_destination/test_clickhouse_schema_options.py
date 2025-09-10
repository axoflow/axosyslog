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
from axosyslog_light.common.file import copy_shared_file
from axosyslog_light.helpers.clickhouse.clickhouse_server_executor import CLICKHOUSE_OPTIONS


custom_input_msg = f"test message {str(uuid.uuid4())}"


def configure_syslog_ng_with_clickhouse_dest(config, additional_clickhouse_options, clickhouse_ports):
    generator_source = config.create_example_msg_generator_source(num=1, template=f'"{custom_input_msg}"')
    clickhouse_options_copy = CLICKHOUSE_OPTIONS.copy()
    clickhouse_options_copy.update({"url": f"'127.0.0.1:{clickhouse_ports.grpc_port}'"})
    if "proto_var" in additional_clickhouse_options:
        filterx = config.create_filterx(
            '''
            $proto_var_value = %s;
        ''' % (additional_clickhouse_options["proto_var"]),
        )
        clickhouse_options_copy.update({"proto_var": "$proto_var_value"})
        clickhouse_destination = config.create_clickhouse_destination(**clickhouse_options_copy)
        config.create_logpath(statements=[generator_source, filterx, clickhouse_destination])
    else:
        clickhouse_options_copy.update(additional_clickhouse_options)
        clickhouse_destination = config.create_clickhouse_destination(**clickhouse_options_copy)
        config.create_logpath(statements=[generator_source, clickhouse_destination])
    clickhouse_destination.create_clickhouse_client(clickhouse_ports.http_port)
    return config, clickhouse_destination


def bootstrap_clickhouse_server(clickhouse_server, clickhouse_destination, clickhouse_options, request, clickhouse_ports):
    clickhouse_server.start(clickhouse_ports)
    clickhouse_destination.create_table(clickhouse_options["table"], [("message", "String")])
    request.addfinalizer(lambda: clickhouse_destination.delete_table())


def check_syslog_ng_start_error(syslog_ng, config, syslog_ng_start_error):
    with pytest.raises(Exception) as exec_info:
        syslog_ng.start(config)
    assert syslog_ng_start_error in str(exec_info.value)


def check_message_arrival(clickhouse_destination, custom_input_msg, message_arrived_in_db, message_arrival_error, syslog_ng):
    if message_arrival_error:
        for error in message_arrival_error:
            assert syslog_ng.wait_for_message_in_console_log(error) != []
    else:
        assert syslog_ng.wait_for_message_in_console_log("ClickHouse batch delivered") != []

    if message_arrived_in_db:
        if "schema" in clickhouse_destination.options and clickhouse_destination.options["schema"] == '"id" "String" => $Unknown':
            # as the $Unknown macro does not exist the value will be "" (empty string)
            assert clickhouse_destination.read_logs() == [{"message": ''}]
        else:
            assert clickhouse_destination.read_logs() == [{"message": custom_input_msg}]
        assert clickhouse_destination.get_stats()["written"] == 1
        assert clickhouse_destination.get_stats()["dropped"] == 0
    else:
        assert clickhouse_destination.read_logs() == []
        assert clickhouse_destination.get_stats()["written"] == 0
        if message_arrival_error == ["FILTERX ERROR"]:
            assert clickhouse_destination.get_stats()["dropped"] == 0
        else:
            assert clickhouse_destination.get_stats()["dropped"] == 1


schema_options = [
    ('"id" "String" =>', False, 'syslog-ng config syntax error', False, None),
    ('', False, 'syslog-ng is not running', False, None),
    ('"$id" "String" => $MSG', False, 'syslog-ng is not running', False, None),
    ('"id" "String" => $Unknown', True, '', True, None),
    ('"id" "Unknown" => $MSG', False, 'syslog-ng config syntax error', False, None),
    ('"Unknown" "String" => $MSG', True, '', True, None),
    ('"id" "String" => $MSG', True, '', True, None),
]


@pytest.mark.parametrize("schema_option_value, syslog_ng_started, syslog_ng_start_error, message_arrived_in_db, message_arrival_error", schema_options, ids=range(len(schema_options)))
def test_clickhouse_destination_schema_option(request, config, syslog_ng, clickhouse_server, schema_option_value, syslog_ng_started, syslog_ng_start_error, message_arrived_in_db, message_arrival_error, clickhouse_ports):
    config, clickhouse_destination = configure_syslog_ng_with_clickhouse_dest(config, {"schema": schema_option_value}, clickhouse_ports)

    if not syslog_ng_started:
        check_syslog_ng_start_error(syslog_ng, config, syslog_ng_start_error)
    else:
        bootstrap_clickhouse_server(clickhouse_server, clickhouse_destination, CLICKHOUSE_OPTIONS, request, clickhouse_ports)
        syslog_ng.start(config)
        check_message_arrival(clickhouse_destination, custom_input_msg, message_arrived_in_db, message_arrival_error, syslog_ng)


protobuf_schema_options = [
    ('unknown.proto => "$MSG"', False, 'syslog-ng config syntax error', False, ''),
    ('clickhouse.proto => "$MSG"', True, '', True, None),
]


@pytest.mark.parametrize("protobuf_schema_option_value, syslog_ng_started, syslog_ng_start_error, message_arrived_in_db, message_arrival_error", protobuf_schema_options, ids=range(len(protobuf_schema_options)))
def test_clickhouse_destination_protobuf_schema_option(request, testcase_parameters, config, syslog_ng, clickhouse_server, protobuf_schema_option_value, syslog_ng_started, syslog_ng_start_error, message_arrived_in_db, message_arrival_error, clickhouse_ports):
    config, clickhouse_destination = configure_syslog_ng_with_clickhouse_dest(config, {"protobuf_schema": protobuf_schema_option_value}, clickhouse_ports)

    if not syslog_ng_started:
        check_syslog_ng_start_error(syslog_ng, config, syslog_ng_start_error)
    else:
        copy_shared_file(testcase_parameters, "clickhouse.proto")
        bootstrap_clickhouse_server(clickhouse_server, clickhouse_destination, CLICKHOUSE_OPTIONS, request, clickhouse_ports)
        syslog_ng.start(config)
        check_message_arrival(clickhouse_destination, custom_input_msg, message_arrived_in_db, message_arrival_error, syslog_ng)


server_side_schema_options = [
    ("Unknown", True, '', False, ['ClickHouse server responded with an exception']),
    ("Unknown:Unknown", False, 'syslog-ng config syntax error', False, ''),
    ("'Unknown:Unknown'", True, '', False, ['ClickHouse server responded with an exception']),
    ("'clickhouse:TestProto'", True, '', True, None),
]


@pytest.mark.parametrize("server_side_schema_option_value, syslog_ng_started, syslog_ng_start_error, message_arrived_in_db, message_arrival_error", server_side_schema_options, ids=range(len(server_side_schema_options)))
def test_clickhouse_destination_server_side_schema_option(request, testcase_parameters, config, syslog_ng, clickhouse_server, server_side_schema_option_value, syslog_ng_started, syslog_ng_start_error, message_arrived_in_db, message_arrival_error, clickhouse_ports):
    config, clickhouse_destination = configure_syslog_ng_with_clickhouse_dest(config, {"server_side_schema": server_side_schema_option_value, "schema": '"message" "String" => "$MSG"'}, clickhouse_ports)

    if not syslog_ng_started:
        check_syslog_ng_start_error(syslog_ng, config, syslog_ng_start_error)
    else:
        copy_shared_file(testcase_parameters, "clickhouse.proto")
        bootstrap_clickhouse_server(clickhouse_server, clickhouse_destination, CLICKHOUSE_OPTIONS, request, clickhouse_ports)
        syslog_ng.start(config)
        check_message_arrival(clickhouse_destination, custom_input_msg, message_arrived_in_db, message_arrival_error, syslog_ng)


proto_var_options = [
    ('protobuf_message({"name": "value"}, schema_file="")', False, 'syslog-ng config syntax error', False, ['']),
    ('protobuf_message({"": ""}, schema_file="clickhouse.proto")', True, '', False, ['FILTERX ERROR']),
    ('protobuf_message({"message": $MSG}, schema_file="clickhouse.proto")', True, '', True, None),
]


@pytest.mark.parametrize("proto_var_option_value, syslog_ng_started, syslog_ng_start_error, message_arrived_in_db, message_arrival_error", proto_var_options, ids=range(len(proto_var_options)))
def test_clickhouse_destination_proto_var_option(request, testcase_parameters, config, syslog_ng, clickhouse_server, proto_var_option_value, syslog_ng_started, syslog_ng_start_error, message_arrived_in_db, message_arrival_error, clickhouse_ports):
    config, clickhouse_destination = configure_syslog_ng_with_clickhouse_dest(config, {"proto_var": proto_var_option_value}, clickhouse_ports)

    if not syslog_ng_started:
        check_syslog_ng_start_error(syslog_ng, config, syslog_ng_start_error)
    else:
        copy_shared_file(testcase_parameters, "clickhouse.proto")
        bootstrap_clickhouse_server(clickhouse_server, clickhouse_destination, CLICKHOUSE_OPTIONS, request, clickhouse_ports)
        syslog_ng.start(config)
        check_message_arrival(clickhouse_destination, custom_input_msg, message_arrived_in_db, message_arrival_error, syslog_ng)
