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
import pytest
from axosyslog_light.common.file import copy_shared_file
from inputs_and_outputs_for_complex_types import all_protobuf_types_expected_data
from inputs_and_outputs_for_complex_types import all_protobuf_types_input_data
from inputs_and_outputs_for_complex_types import all_protobuf_types_proto_file_data
from inputs_and_outputs_for_complex_types import all_protobuf_types_table_schema
from inputs_and_outputs_for_complex_types import clickhouse_server_types_expected_data
from inputs_and_outputs_for_complex_types import clickhouse_server_types_input_data
from inputs_and_outputs_for_complex_types import clickhouse_server_types_proto_file_data
from inputs_and_outputs_for_complex_types import clickhouse_server_types_table_schema
from inputs_and_outputs_for_complex_types import complex_types_input_data
from inputs_and_outputs_for_complex_types import complex_types_proto_file_data
from inputs_and_outputs_for_complex_types import complex_types_table_schema

clickhouse_options = {
    "database": "default",
    "table": "test_table",
    "user": "default",
    "password": "'password'",
}


def configure_syslog_ng_with_clickhouse_dest(config, proto_file_data, clickhouse_ports):
    file_source = config.create_file_source(file_name="input.log")
    clickhouse_options_copy = clickhouse_options.copy()
    filterx_expr = f'''
     $proto_var_value = protobuf_message(json($MSG), schema_file="{proto_file_data['proto_file_name']}");
     $MESSAGE = {{"message": $MSG}};
'''
    filterx = config.create_filterx(filterx_expr)
    clickhouse_options_copy.update({"proto_var": "$proto_var_value"})
    clickhouse_options_copy.update({"server_side_schema": config.stringify(proto_file_data["server_side_schema_value"])})
    clickhouse_options_copy.update({"url": f"'127.0.0.1:{clickhouse_ports.grpc_port}'"})
    clickhouse_destination = config.create_clickhouse_destination(**clickhouse_options_copy)
    clickhouse_destination.create_clickhouse_client(clickhouse_ports.http_port)
    config.create_logpath(statements=[file_source, filterx, clickhouse_destination])
    return config, file_source, clickhouse_destination


def bootstrap_clickhouse_server(clickhouse_server, clickhouse_destination, clickhouse_options, request, clickhouse_table_schema, clickhouse_ports):
    clickhouse_server.start(clickhouse_ports)
    clickhouse_destination.create_table(clickhouse_options["table"], clickhouse_table_schema)
    request.addfinalizer(lambda: clickhouse_destination.delete_table())


tc_variants = [
    (complex_types_input_data, complex_types_table_schema, complex_types_proto_file_data, complex_types_input_data),
    (all_protobuf_types_input_data, all_protobuf_types_table_schema, all_protobuf_types_proto_file_data, all_protobuf_types_expected_data),
    (clickhouse_server_types_input_data, clickhouse_server_types_table_schema, clickhouse_server_types_proto_file_data, clickhouse_server_types_expected_data),
]


@pytest.mark.parametrize("input_data, table_schema, proto_file_data, expected_output_data", tc_variants, ids=range(len(tc_variants)))
def test_clickhouse_destination_complex_types(request, testcase_parameters, config, syslog_ng, clickhouse_server, input_data, table_schema, proto_file_data, expected_output_data, clickhouse_ports):
    copy_shared_file(testcase_parameters, proto_file_data["proto_file_name"])

    config, file_source, clickhouse_destination = configure_syslog_ng_with_clickhouse_dest(config, proto_file_data, clickhouse_ports)
    bootstrap_clickhouse_server(clickhouse_server, clickhouse_destination, clickhouse_options, request, table_schema, clickhouse_ports)

    input_msg = f'<113>Jul 17 14:47:53 testhost testprog {input_data}'
    file_source.write_log(input_msg, 1)
    syslog_ng.start(config)

    assert syslog_ng.wait_for_message_in_console_log("ClickHouse batch delivered") != []
    assert clickhouse_destination.read_logs() == [expected_output_data]
    assert clickhouse_destination.get_stats()["written"] == 1
    assert clickhouse_destination.get_stats()["dropped"] == 0
