#!/usr/bin/env python
#############################################################################
# Copyright (c) 2025 Axoflow
# Copyright (c) 2025 László Várady
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
import json

import pytest


def test_filterx_failure_info(syslog_ng, config):
    source = config.create_example_msg_generator_source(num=1, template='"test message"')
    fx_failure_info_enable = config.create_filterx("failure_info_enable(collect_falsy=true);")

    fx_error = config.create_filterx(r"""
        failure_info_meta({"step": "step_1"});
        a = 3;

        failure_info_meta({"step": "step_2"});
        nonexisting.key = a;
    """)
    error_path = config.create_inner_logpath(statements=[fx_error])

    fx_falsy = config.create_filterx(r"""
        failure_info_meta({"step": "falsy_block"});
        a = 4;
        a == 3;
    """)
    falsy_path = config.create_inner_logpath(statements=[fx_falsy])

    fx_failure_info_collect = config.create_filterx("$failure_info = failure_info();")
    destination = config.create_file_destination(file_name="output.log", template='"${failure_info}\n"')
    last_path = config.create_inner_logpath(statements=[fx_failure_info_collect, destination])

    config.create_logpath(statements=[source, fx_failure_info_enable, error_path, falsy_path, last_path])

    syslog_ng.start(config)

    actual_failure_info = json.loads(destination.read_log())
    assert len(actual_failure_info) == 2

    actual_error = actual_failure_info[0]
    assert actual_error["errors"] == [
        {"location": "syslog_ng_server.conf:31:21", "line": "nonexisting", "error": "No such variable: nonexisting"},
        {"location": "syslog_ng_server.conf:31:21", "line": "nonexisting.key = a", "error": "Failed to set-attribute to object: Failed to evaluate expression"},
    ]
    assert actual_error["meta"]["step"] == "step_2"

    actual_falsy = actual_failure_info[1]
    assert actual_falsy["errors"] == [
        {"location": "syslog_ng_server.conf:41:21", "line": "a == 3", "error": "bailing out due to a falsy expr: false"},
    ]
    assert actual_falsy["meta"]["step"] == "falsy_block"


@pytest.mark.parametrize(
    "filterx_value_type, failure_info_error", [
        ("meta.a = bool($MSG);", 'repr() failed: {"a":true}'),
        ("meta.a = bytes($MSG);", 'repr() failed: {"a":"dGVzdCBtZXNzYWdl"}'),
        ("meta.a = datetime('2006-02-11T10:34:56.000+01:00');", 'repr() failed: {\"a\":\"1139654096.000000\"}'),
        ("meta.a = dedup_metrics_labels(metrics_labels());", 'repr() failed: {"a":true}'),
        ("meta.a = dict({'msg': $MSG});", 'repr() failed: {"a":{"msg":"test message"}'),
        ("meta.a = double(42);", 'repr() failed: {\"a\":42.0}'),
        ("meta.a = format_isodate(datetime('2006-02-11T10:34:56.000+01:00'));", "repr() failed: {\"a\":\"2006-02-11T10:34:56.000000+00:00\"}"),
        ("meta.a = format_json($MSG);", "repr() failed: {\"a\":\"\\\"test message\\\"\"}"),
        ("meta.a = get_sdata();", 'repr() failed: {"a":{}}'),
        ("meta.a = has_sdata();", 'repr() failed: {"a":false}'),
        ("meta.a = int(42);", "repr() failed: {\"a\":42}"),
        ("meta.a = isodate('2006-02-11T10:34:56.000+01:00');", "repr() failed: {\"a\":\"1139654096.000000\"}"),
        ("meta.a = json_array(list([$MSG]));", 'repr() failed: {"a":["test message"]}'),
        ("meta.a = json({'msg': $MSG});", 'repr() failed: {"a":{"msg":"test message"}}'),
        ("meta.a = len($MSG);", 'repr() failed: {"a":12}'),
        ("meta.a = list([$MSG]);", 'repr() failed: {"a":["test message"]}'),
        ("meta.a = lower($MSG);", 'repr() failed: {"a":"test message"}'),
        ("meta.a = $MSG;", 'repr() failed: {"a":"test message"}'),
        ("meta.a = metrics_labels();", 'repr() failed: {"a":{}}'),
        ("meta.a = null;", 'repr() failed: {"a":null}'),
        ("meta.a = otel_array([1, 2]);", 'repr() failed: {"a":[1,2]}'),
        ("meta.a = otel_kvlist({'foo': 42});", 'repr() failed: {"a":{"foo":42}}'),
        ("meta.a = otel_logrecord();", 'repr() failed: {"a":{}}'),
        ("meta.a = otel_resource();", 'repr() failed: {"a":{}}'),
        ("meta.a = otel_scope({'name': 'my_scope'});", 'repr() failed: {"a":{"name":"my_scope"}}'),
        ("meta.a = pubsub_message('msg', {'k': 'v'});", 'repr() failed: {"a":{"data":"bXNn","attributes":{"k":"v"}}}'),
        ("meta.a = parse_json(string({'msg':$MSG}));", 'repr() failed: {"a":{"msg":"test message"}}'),
        ("meta.a = protobuf(bytes($MSG));", 'repr() failed: {"a":"dGVzdCBtZXNzYWdl"}'),
        ("meta.a = repr($MSG);", 'repr() failed: {"a":"test message"}'),
        ("meta.a = str_lstrip($MSG, 'cat', 'dog');", "str_lstrip(): Requires exactly one argument"),
        ("meta.a = str_replace($MSG, 'cat', 'dog');", 'repr() failed: {"a":"test message"}'),
        ("meta.a = str_rstrip($MSG, 'cat', 'dog');", "str_rstrip(): Requires exactly one argument"),
        ("meta.a = str_strip($MSG, 'cat', 'dog');", "str_strip(): Requires exactly one argument"),
        ("meta.a = string($MSG);", 'repr() failed: {"a":"test message"}'),
        ("meta.a = upper($MSG);", 'repr() failed: {"a":"TEST MESSAGE"}'),
        ("_ref_val = {}; meta.a = _ref_val;", 'repr() failed: {"a":{}}'),
    ], ids=[
        "filterx_type_bool",
        "filterx_type_bytes",
        "filterx_type_datetime",
        "filterx_type_dedup_metrics_labels",
        "filterx_type_dict",
        "filterx_type_double",
        "filterx_type_format_isodate",
        "filterx_type_format_json",
        "filterx_type_get_sdata",
        "filterx_type_has_sdata",
        "filterx_type_int",
        "filterx_type_isodate",
        "filterx_type_json_array",
        "filterx_type_json",
        "filterx_type_len",
        "filterx_type_list",
        "filterx_type_lower",
        "filterx_type_message_value",
        "filterx_type_metrics_labels",
        "filterx_type_null",
        "filterx_type_otel_array",
        "filterx_type_otel_kvlist",
        "filterx_type_otel_logrecord",
        "filterx_type_otel_resource",
        "filterx_type_otel_scope",
        "filterx_type_pubsub_message",
        "filterx_type_parse_json",
        "filterx_type_protobuf",
        "filterx_type_repr",
        "filterx_type_str_lstrip",
        "filterx_type_str_replace",
        "filterx_type_str_rstrip",
        "filterx_type_str_strip",
        "filterx_type_string",
        "filterx_type_upper",
        "filterx_type_ref",
    ],
)
def test_all_filterx_types_in_failure_info(syslog_ng, config, filterx_value_type, failure_info_error):
    input_message = "test message"
    source = config.create_example_msg_generator_source(num=1, template=f'"{input_message}"')

    fx_failure_info_enable = config.create_filterx("failure_info_enable(collect_falsy=true);")

    filterx_expr = 'meta = {}; \n'
    filterx_expr += filterx_value_type + " \n"
    filterx_expr += 'includes(meta, "mypattern"); \n'

    filterx_block = config.create_filterx(filterx_expr)
    if_block = config.create_logpath_if(if_elif_blocks=[filterx_block])

    fx_failure_info_collect = config.create_filterx("$failure_info = failure_info();")
    destination = config.create_file_destination(file_name="output.log")
    last_path = config.create_inner_logpath(statements=[fx_failure_info_collect, destination])

    config.create_logpath(statements=[source, fx_failure_info_enable, if_block, last_path])

    syslog_ng.start(config)

    assert input_message in destination.read_log()
    assert syslog_ng.wait_for_message_in_console_log(failure_info_error)
