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
import logging
import time
from pathlib import Path

import psutil
from axosyslog_light.common.blocking import wait_until_true_custom
from axosyslog_light.common.file import copy_shared_file
from axosyslog_light.helpers.loggen.loggen import LoggenStartParams
from axosyslog_light.syslog_ng_config.__init__ import stringify
from generate_logs import generate_cef_message
from generate_logs import generate_csv_message
from generate_logs import generate_eventlog_xml_message
from generate_logs import generate_json_message
from generate_logs import generate_kv_message
from generate_logs import generate_leef_message
from generate_logs import generate_parsed_cef_json_message
from generate_logs import generate_parsed_leef_json_message


logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)
input_message_file_name = Path("input_message.txt")
file_dst_output_name = Path("file_dst_output.txt")


def send_msg_wait_for_finish(loggen, network_source, msg_counter, run_time, input_msg_type, msg_size):
    input_msg = ""
    for i in range(100):
        if input_msg_type == "json":
            input_msg += generate_json_message(msg_size)
        elif input_msg_type == "cef":
            input_msg += generate_cef_message(msg_size)
        elif input_msg_type == "leef":
            input_msg += generate_leef_message(msg_size)
        elif input_msg_type == "kv":
            input_msg += generate_kv_message(msg_size)
        elif input_msg_type == "csv":
            input_msg += generate_csv_message(msg_size)
        elif input_msg_type == "xml":
            input_msg += generate_eventlog_xml_message(msg_size)
        elif input_msg_type == "parsed-cef":
            input_msg += generate_parsed_cef_json_message(msg_size)
        elif input_msg_type == "parsed-leef":
            input_msg += generate_parsed_leef_json_message(msg_size)

    with open(input_message_file_name, "w") as f:
        if msg_counter != 0:
            for i in range(int(msg_counter / 100)):
                f.write(input_msg)

            loggen.start(
                LoggenStartParams(
                    target=network_source.options["ip"],
                    port=network_source.options["port"],
                    inet=True,
                    perf=True,
                    active_connections=1,
                    number=msg_counter,
                    read_file=input_message_file_name,
                    dont_parse=True,
                    loop_reading=False,
                ),
            )
            assert wait_until_true_custom(lambda: loggen.get_sent_message_count() == msg_counter, timeout=300)

        else:
            f.write(input_msg)
            loggen_proc = loggen.start(
                LoggenStartParams(
                    target=network_source.options["ip"],
                    port=network_source.options["port"],
                    inet=True,
                    perf=True,
                    active_connections=1,
                    interval=run_time,
                    read_file=input_message_file_name,
                    dont_parse=True,
                    loop_reading=True,
                ),
            )
            assert wait_until_true_custom(lambda: loggen_proc.poll() == 0, timeout=run_time + 5)


def start_syslog_ng(syslog_ng, config):
    syslog_ng.start_params.trace = False
    syslog_ng.start_params.debug = False
    syslog_ng.start_params.verbose = False
    syslog_ng.start(config)
    time.sleep(0.5)


def build_modifier(modifier, config, config_statements, input_msg):
    if modifier == "csv-parser-delimiter":
        modifier = config.create_csv_parser(prefix=config.stringify("prefix."), delimiters=config.stringify("|"))

    if modifier == "csv-parser-columns":
        number_of_delimiters = input_msg.count("|")
        column_names = ", ".join(f"'{i + 1}'" for i in range(number_of_delimiters + 1))
        modifier = config.create_csv_parser(prefix=config.stringify("prefix."), columns=column_names, delimiters=config.stringify("|"))

    config_statements.append(modifier)
    return config_statements


def build_filterx_rule(filterx_rule, config, config_statements, input_msg):
    filterx_expr = ""
    if filterx_rule == "filterx-builtin-simple-functions":
        filterx_expr = "$log = {'data': {}};"
        filterx_expr += "$log.data['bool'] = bool($MSG);"
        filterx_expr += "$log.data['bytes'] = bytes($MSG);"
        filterx_expr += "$log.data['datetime'] = string(datetime($ISODATE));"
        filterx_expr += "$log.data['dedup_metrics_labels'] = dedup_metrics_labels(metrics_labels());"
        filterx_expr += "$log.data['dict'] = dict({'msg': $MSG});"
        filterx_expr += "$log.data['double'] = double($R_SEC);"
        filterx_expr += "$log.data['format_json'] = format_json($MSG);"
        filterx_expr += "$log.data['get_sdata'] = get_sdata();"
        filterx_expr += "$log.data['has_sdata'] = has_sdata();"
        filterx_expr += "$log.data['int'] = int($R_SEC);"
        filterx_expr += "$log.data['isodate'] = string(isodate($ISODATE));"
        filterx_expr += "$log.data['json_array'] = json_array(list([$MSG]));"
        filterx_expr += "$log.data['json'] = json({'msg': $MSG});"
        filterx_expr += "$log.data['len'] = len($MSG);"
        filterx_expr += "$log.data['list'] = list([$MSG]);"
        filterx_expr += "$log.data['load_vars'] = load_vars($log);"
        filterx_expr += "$log.data['lower'] = lower($MSG);"
        filterx_expr += "$log.data['metrics_labels'] = metrics_labels();"
        filterx_expr += "$log.data['parse_json'] = parse_json(string({'msg':$MSG}));"
        filterx_expr += "$log.data['protobuf'] = protobuf(bytes($MSG));"
        filterx_expr += "$log.data['repr'] = repr($MSG);"
        filterx_expr += "$log.data['str_replace'] = str_replace($MSG, 'cat', 'dog');"
        filterx_expr += "$log.data['string'] = string($MSG);"
        filterx_expr += "$log.data['upper'] = upper($MSG);"
        filterx_expr += "$MESSAGE = $log;"

    if filterx_rule == "filterx-parse-cef":
        filterx_expr = "cef = parse_cef($MSG);\n"
        filterx_expr += "$MESSAGE = cef;"

    if filterx_rule == "filterx-parse-csv":
        filterx_expr += "$log = parse_csv($MSG, delimiter=',');\n"
        filterx_expr += "$MESSAGE = $log;"

    if filterx_rule == "filterx-parse-kv":
        filterx_expr = "kv = parse_kv($MSG);\n"
        filterx_expr += "$MESSAGE = kv;"

    if filterx_rule == "filterx-parse-leef":
        filterx_expr = "leef = parse_leef($MSG);\n"
        filterx_expr += "$MESSAGE = leef;"

    if filterx_rule == "filterx-parse-windows-eventlog-xml":
        filterx_expr = "xml = parse_windows_eventlog_xml($MSG);\n"
        filterx_expr += "$MESSAGE = xml;"

    if filterx_rule == "filterx-parse-xml":
        filterx_expr = "xml = parse_xml($MSG);\n"
        filterx_expr += "$MESSAGE = xml;"

    if filterx_rule == "filterx-format-cef":
        filterx_expr = "cef = format_cef(json($MSG));\n"
        filterx_expr += "$MESSAGE = cef;"

    if filterx_rule == "filterx-format-csv":
        filterx_expr = "csv = format_csv(json($MSG));\n"
        filterx_expr += "$MESSAGE = csv;"

    if filterx_rule == "filterx-format-kv":
        filterx_expr = "kv = format_kv(json($MSG));\n"
        filterx_expr += "$MESSAGE = kv;"

    if filterx_rule == "filterx-format-leef":
        filterx_expr = "leef = format_leef(json($MSG));\n"
        filterx_expr += "$MESSAGE = leef;"

    if filterx_rule == "filterx-format-windows-eventlog-xml":
        filterx_expr = "xml = format_windows_eventlog_xml(json($MSG));\n"
        filterx_expr += "$MESSAGE = xml;"

    if filterx_rule == "filterx-format-xml":
        filterx_expr = "xml = format_xml(json($MSG));\n"
        filterx_expr += "$MESSAGE = xml;"

    if filterx_rule == "filterx-from-csv-parser-input":
        filterx_expr = "$log = {};\n"
        number_of_delimiters = input_msg.count("|")
        for i in range(number_of_delimiters + 1):
            filterx_expr += f'$log["{i + 1}"] = ".${i}";\n'
        filterx_expr += '$MESSAGE = {"message": $log};\n'

    if filterx_rule == "filterx-with-parse-csv-and-format-json":
        column_names = []
        number_of_delimiters = input_msg.count("|")
        for i in range(number_of_delimiters + 1):
            column_names.append(f"column_{i}")
        filterx_expr = f"defined_columns = {column_names};"
        filterx_expr += "$log = parse_csv($MSG, delimiter='|', columns=defined_columns);"
        filterx_expr += "$MESSAGE = format_json($log);"

    filterx = config.create_filterx(filterx_expr)
    config_statements.append(filterx)
    return config_statements


def set_filterx_for_builtin_simple_functions(config, config_statements, testcase_parameters):
    filterx_expr = """
serialized_message = {
    "MSG_DATA_bool": $log.data.bool,
    "MSG_DATA_bytes": $log.data.bytes,
    "MSG_DATA_datetime": $log.data.datetime,
    "MSG_DATA_dedup_metrics_labels": $log.data.dedup_metrics_labels,
    "MSG_DATA_dict": $log.data.dict,
    "MSG_DATA_double": $log.data.double,
    "MSG_DATA_format_json": $log.data.format_json,
    "MSG_DATA_get_sdata": $log.data.get_sdata,
    "MSG_DATA_has_sdata": $log.data.has_sdata,
    "MSG_DATA_int": $log.data.int,
    "MSG_DATA_isodate": $log.data.isodate,
    "MSG_DATA_json_array": $log.data.json_array,
    "MSG_DATA_json": $log.data.json,
    "MSG_DATA_len": $log.data.len,
    "MSG_DATA_list": $log.data.list,
    "MSG_DATA_load_vars": $log.data.load_vars,
    "MSG_DATA_lower": $log.data.lower,
    "MSG_DATA_metrics_labels": $log.data.metrics_labels,
    "MSG_DATA_parse_json": $log.data.parse_json,
    "MSG_DATA_protobuf": $log.data.protobuf,
    "MSG_DATA_repr": $log.data.repr,
    "MSG_DATA_str_replace": $log.data.str_replace,
    "MSG_DATA_string": $log.data.string,
    "MSG_DATA_upper": $log.data.upper,
};
$protobuf_message = protobuf_message(serialized_message, schema_file="clickhouse_filterx_builtins_simple_functions.proto");
$MESSAGE = {"message": serialized_message};
        """
    filterx = config.create_filterx(filterx_expr)
    config_statements.append(filterx)
    copy_shared_file(testcase_parameters, "clickhouse_filterx_builtins_simple_functions.proto")


def set_filterx_for_parse(config, config_statements, testcase_parameters):
    filterx_expr = """
serialized_message = {
    "MSG_DATA_json": $MSG,
};
$protobuf_message = protobuf_message(serialized_message, schema_file="clickhouse_filterx_parse.proto");
$MESSAGE = {"message": serialized_message};
        """
    filterx = config.create_filterx(filterx_expr)
    config_statements.append(filterx)
    copy_shared_file(testcase_parameters, "clickhouse_filterx_parse.proto")


def set_filterx_for_format(config, config_statements, testcase_parameters):
    filterx_expr = """
serialized_message = {
    "MSG_DATA_string": $MSG,
};
$protobuf_message = protobuf_message(serialized_message, schema_file="clickhouse_filterx_format.proto");
$MESSAGE = {"message": serialized_message};
        """
    filterx = config.create_filterx(filterx_expr)
    config_statements.append(filterx)
    copy_shared_file(testcase_parameters, "clickhouse_filterx_format.proto")


def build_clickhouse_config_options(destination, clickhouse_ports):
    clickhouse_options = {
        "database": "default",
        "table": "test_table",
        "user": "default",
        "password": f'{stringify("password")}',
        "url": f"'127.0.0.1:{clickhouse_ports.grpc_port}'",
        "workers": 4,
        "batch_lines": 1000,
        "batch_timeout": 1000,
        "log_fifo_size": 100000,
    }
    if destination == "clickhouse-dst-for-filterx-builtin-simple-functions":
        clickhouse_options.update({
            "server_side_schema": "'clickhouse_filterx_builtins_simple_functions:TestPerfProto'",
            "proto_var": "$protobuf_message",
        })
    elif destination == "clickhouse-dst-for-filterx-parse":
        clickhouse_options.update({
            "server_side_schema": "'clickhouse_filterx_parse:TestPerfProto'",
            "proto_var": "$protobuf_message",
        })
    elif destination == "clickhouse-dst-for-filterx-format":
        clickhouse_options.update({
            "server_side_schema": "'clickhouse_filterx_format:TestPerfProto'",
            "proto_var": "$protobuf_message",
        })
    else:
        clickhouse_options.update({
            "schema": '"message" "String" => "$MSG"',
        })
    return clickhouse_options


def create_clickhouse_table_for_destination_case(destination, clickhouse_destination, clickhouse_options, request):
    if destination == "clickhouse-dst-for-filterx-builtin-simple-functions":
        clickhouse_destination.create_table(
            clickhouse_options["table"], [
                ("MSG_DATA_bool", "BOOL"),
                ("MSG_DATA_bytes", "String"),
                ("MSG_DATA_datetime", "DateTime64(3, 'Europe/Budapest')"),
                ("MSG_DATA_dedup_metrics_labels", "BOOL"),
                ("MSG_DATA_dict", "Map(String, String)"),
                ("MSG_DATA_double", "DOUBLE"),
                ("MSG_DATA_format_json", "String"),
                ("MSG_DATA_get_sdata", "Map(String, String)"),
                ("MSG_DATA_has_sdata", "BOOL"),
                ("MSG_DATA_int", "Int32"),
                ("MSG_DATA_isodate", "DateTime64(3, 'Europe/Budapest')"),
                ("MSG_DATA_json_array", "Array(String)"),
                ("MSG_DATA_json", "Map(String, String)"),
                ("MSG_DATA_len", "Int32"),
                ("MSG_DATA_list", "Array(String)"),
                ("MSG_DATA_load_vars", "BOOL"),
                ("MSG_DATA_lower", "String"),
                ("MSG_DATA_metrics_labels", "Map(String, String)"),
                ("MSG_DATA_parse_json", "Map(String, String)"),
                ("MSG_DATA_protobuf", "String"),
                ("MSG_DATA_repr", "String"),
                ("MSG_DATA_str_replace", "String"),
                ("MSG_DATA_string", "String"),
                ("MSG_DATA_upper", "String"),
            ],
        )
    elif destination == "clickhouse-dst-for-filterx-parse":
        clickhouse_destination.create_table(
            clickhouse_options["table"], [
                ("MSG_DATA_json", "Map(String, String)"),
            ],
        )
    elif destination == "clickhouse-dst-for-filterx-format":
        clickhouse_destination.create_table(
            clickhouse_options["table"], [
                ("MSG_DATA_string", "String"),
            ],
        )
    else:
        clickhouse_destination.create_table(clickhouse_options["table"], [("message", "String")])
    request.addfinalizer(lambda: clickhouse_destination.delete_table())


def build_destination(destination, config, config_statements, clickhouse_server, clickhouse_ports, request, testcase_parameters):
    destination_driver = None
    if "for-filterx-builtin-simple-functions" in destination:
        set_filterx_for_builtin_simple_functions(config, config_statements, testcase_parameters)

    if "for-filterx-parse" in destination:
        set_filterx_for_parse(config, config_statements, testcase_parameters)

    if "for-filterx-format" in destination:
        set_filterx_for_format(config, config_statements, testcase_parameters)

    if "file-dst" in destination:
        file_destination = config.create_file_destination(file_name=str(file_dst_output_name))
        destination_driver = file_destination
        config_statements.append(file_destination)

    if "clickhouse-dst" in destination:
        clickhouse_options = build_clickhouse_config_options(destination, clickhouse_ports)
        clickhouse_destination = config.create_clickhouse_destination(**clickhouse_options)
        clickhouse_destination.create_clickhouse_client(clickhouse_ports.http_port)
        destination_driver = clickhouse_destination
        config_statements.append(clickhouse_destination)
        clickhouse_server.start(clickhouse_ports)
        create_clickhouse_table_for_destination_case(destination, clickhouse_destination, clickhouse_options, request)

    return config_statements, destination_driver


def cleanup():
    input_message_file_name.unlink()
    try:
        file_dst_output_name.unlink()
    except Exception as e:
        logger.error(f"file destination does not exist: {file_dst_output_name}, error: {e}")


def pytest_generate_tests(metafunc):
    if "input_msg_type" in metafunc.fixturenames:
        metafunc.parametrize("input_msg_type", metafunc.config.getoption("input_msg_type"))
    if "msg_size" in metafunc.fixturenames:
        metafunc.parametrize("msg_size", metafunc.config.getoption("msg_size"))
    if "msg_counter" in metafunc.fixturenames:
        metafunc.parametrize("msg_counter", [metafunc.config.getoption("msg_counter")])
    if "filterx_rule" in metafunc.fixturenames:
        metafunc.parametrize("filterx_rule", metafunc.config.getoption("filterx_rule"))
    if "destination" in metafunc.fixturenames:
        metafunc.parametrize("destination", metafunc.config.getoption("destination"))
    if "flow_control" in metafunc.fixturenames:
        metafunc.parametrize("flow_control", metafunc.config.getoption("flow_control"))
    if "run_time" in metafunc.fixturenames:
        metafunc.parametrize("run_time", [metafunc.config.getoption("run_time")])


def test_performance(request, testcase_parameters, config, syslog_ng, clickhouse_server, clickhouse_ports, port_allocator, loggen, input_msg_type, msg_size, msg_counter, filterx_rule, destination, flow_control, run_time):
    tc_variants_concat = f"{input_msg_type}_{msg_size}_{msg_counter}_{run_time}_{filterx_rule}_{destination}_{flow_control}"

    config_statements = []
    network_source = config.create_network_source(ip="localhost", port=port_allocator(), transport="tcp", log_iw_size=100000, log_fetch_limit=10000)
    config_statements.append(network_source)

    # if modifier:
    #     config_statements = build_modifier(modifier, config, config_statements, input_msg)

    if filterx_rule:
        config_statements = build_filterx_rule(filterx_rule, config, config_statements, input_msg_type)

    if destination:
        config_statements, destination_driver = build_destination(destination, config, config_statements, clickhouse_server, clickhouse_ports, request, testcase_parameters)

    if "on" in flow_control or "yes" in flow_control or "enabled" in flow_control:
        config.create_logpath(statements=config_statements, flags=["flow-control"])
    else:
        config.create_logpath(statements=config_statements)

    start_syslog_ng(syslog_ng, config)
    send_msg_wait_for_finish(loggen, network_source, msg_counter, run_time, input_msg_type, msg_size)
    time.sleep(1)

    try:
        logger.info("destination driver stats for %s: %s", tc_variants_concat, destination_driver.get_stats())
    except Exception as e:
        logger.error("Failed to get destination driver stats for %s: %s", tc_variants_concat, e)

    cpu_percent = psutil.Process(syslog_ng._process.pid).cpu_percent(interval=1)
    logger.info(f"syslog_ng CPU usage: {cpu_percent}%")
    syslog_ng.stop()
    perf_report_log = f"{tc_variants_concat}: {loggen.get_loggen_stats().msg_per_second} msg/sec, {loggen.get_loggen_stats().run_time} run_time"
    logger.info(f"Stats result: {perf_report_log}")
    cleanup()
