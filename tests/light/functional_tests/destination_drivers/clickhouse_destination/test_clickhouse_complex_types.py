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

from axosyslog_light.common.file import copy_shared_file


custom_input_msg = f"test message {str(uuid.uuid4())}"
clickhouse_options = {
    "database": "default",
    "table": "test_table",
    "user": "default",
    "password": "'password'",
}


def configure_syslog_ng_with_clickhouse_dest(config):
    generator_source = config.create_example_msg_generator_source(num=1, template=f'"{custom_input_msg}"')
    clickhouse_options_copy = clickhouse_options.copy()
    filterx = config.create_filterx('''
    message = {
        "id": 9876,
        "message": $MSG,
        "data_map": {"key1": 1, "key2": 2},
        "data_map2": {"key1": {"foo1": "value1", "bar1": 1, "baz1": 1.1}, "key2": {"foo1": "value2", "bar1": 2, "baz1": 2.2}},
        "data_array": [{"key1": "value1"}, {"key2": "value2"}],
        "data_array2": [{"foo2": "value1", "bar2": 1, "baz2": 1.1}, {"foo2": "value2", "bar2": 2, "baz2": 2.2}],
        "data_tuple": {"foo3": "value1", "bar3": 3},
    };
    $proto_var_value = protobuf_message(message, schema_file="clickhouse_nested_types.proto");
    $MESSAGE = {"message": message};
''')
    clickhouse_options_copy.update({"proto_var": "$proto_var_value"})
    clickhouse_options_copy.update({"server_side_schema": "'clickhouse_nested_types:TestProto'"})
    clickhouse_destination = config.create_clickhouse_destination(**clickhouse_options_copy)
    config.create_logpath(statements=[generator_source, filterx, clickhouse_destination])
    return config, clickhouse_destination


def bootstrap_clickhouse_server(clickhouse_server, clickhouse_destination, clickhouse_options, request, testcase_parameters):
    copy_shared_file(testcase_parameters, "clickhouse_nested_types.proto")
    clickhouse_server.start()
    clickhouse_destination.create_table(
        clickhouse_options["table"], [
            ("id", "Int32"),
            ("message", "String"),
            ("data_map", "Map(String, Int32)"),
            ("data_map2", "Map(String, Tuple(foo1 String, bar1 Int32, baz1 Float32))"),
            ("data_array", "Array(String)"),
            ("data_array2", "Array(Tuple(foo2 String, bar2 Int32, baz2 Float32))"),
            ("data_tuple", "Tuple(foo3 String, bar3 Int32)"),
        ],
    )
    request.addfinalizer(lambda: clickhouse_destination.delete_table())


def test_clickhouse_destination_complex_types(request, testcase_parameters, config, syslog_ng, clickhouse_server):
    config, clickhouse_destination = configure_syslog_ng_with_clickhouse_dest(config)
    bootstrap_clickhouse_server(clickhouse_server, clickhouse_destination, clickhouse_options, request, testcase_parameters)
    syslog_ng.start(config)

    assert syslog_ng.wait_for_message_in_console_log("ClickHouse batch delivered") != []
    assert clickhouse_destination.read_logs() == ['9876', custom_input_msg, "{'key1':1,'key2':2}", "{'key1':('value1',1,1.1),'key2':('value2',2,2.2)}", '[\'{"key1":"value1"}\',\'{"key2":"value2"}\']', "[('value1',1,1.1),('value2',2,2.2)]", "('value1',3)"]
    assert clickhouse_destination.get_stats()["written"] == 1
    assert clickhouse_destination.get_stats()["dropped"] == 0


def configure_syslog_ng_with_clickhouse_dest_non_nested_protobuf_types(config):
    generator_source = config.create_example_msg_generator_source(num=1, template=f'"{custom_input_msg}"')
    clickhouse_options_copy = clickhouse_options.copy()
    filterx = config.create_filterx('''
    message = {
        "data_bool": 1,
        "data_bytes": bytes("some random data"),
        "data_double": 3.14,
        "data_enum": 'FOO',
        "data_fixed32": 12345678,
        "data_fixed64": 1234567890123456789,
        "data_float": 2.71,
        "data_int32": -12345678,
        "data_int64": -1234567890123456789,
        "data_sfixed32": -12345678,
        "data_sfixed64": -1234567890123456789,
        "data_sint32": -12345678,
        "data_sint64": -1234567890123456789,
        "data_string": "test_string",
        "data_uint32": 12345678,
        "data_uint64": 1234567890123456789,

    };
    $proto_var_value = protobuf_message(message, schema_file="clickhouse_non_nested_protobuf_types.proto");
    $MESSAGE = {"message": message};
''')
    clickhouse_options_copy.update({"proto_var": "$proto_var_value"})
    clickhouse_options_copy.update({"server_side_schema": "'clickhouse_non_nested_protobuf_types:TestProto'"})
    clickhouse_destination = config.create_clickhouse_destination(**clickhouse_options_copy)
    config.create_logpath(statements=[generator_source, filterx, clickhouse_destination])
    return config, clickhouse_destination


def bootstrap_clickhouse_server_non_nested_protobuf_types(clickhouse_server, clickhouse_destination, clickhouse_options, request, testcase_parameters):
    copy_shared_file(testcase_parameters, "clickhouse_non_nested_protobuf_types.proto")
    clickhouse_server.start()
    clickhouse_destination.create_table(
        clickhouse_options["table"], [
            ("data_bool", "UInt8"),
            ("data_bytes", "String"),
            ("data_double", "Float64"),
            ("data_enum", "Enum('FOO')"),
            ("data_fixed32", "UInt32"),
            ("data_fixed64", "UInt64"),
            ("data_float", "Float32"),
            ("data_int32", "Int32"),
            ("data_int64", "Int64"),
            ("data_sfixed32", "Int32"),
            ("data_sfixed64", "Int64"),
            ("data_sint32", "Int32"),
            ("data_sint64", "Int64"),
            ("data_string", "String"),
            ("data_uint32", "UInt32"),
            ("data_uint64", "UInt64"),
        ],
    )
    request.addfinalizer(lambda: clickhouse_destination.delete_table())


def test_clickhouse_destination_all_protobuf_types(request, testcase_parameters, config, syslog_ng, clickhouse_server):
    config, clickhouse_destination = configure_syslog_ng_with_clickhouse_dest_non_nested_protobuf_types(config)
    bootstrap_clickhouse_server_non_nested_protobuf_types(clickhouse_server, clickhouse_destination, clickhouse_options, request, testcase_parameters)
    syslog_ng.start(config)

    assert syslog_ng.wait_for_message_in_console_log("ClickHouse batch delivered") != []
    assert clickhouse_destination.read_logs() == ['1', 'some random data', '3.14', '12345678', '1234567890123456789', '2.71', '-12345678', '-1234567890123456789', '-12345678', '-1234567890123456789', '-12345678', '-1234567890123456789', 'test_string', '12345678', '1234567890123456789']
    assert clickhouse_destination.get_stats()["written"] == 1
    assert clickhouse_destination.get_stats()["dropped"] == 0
