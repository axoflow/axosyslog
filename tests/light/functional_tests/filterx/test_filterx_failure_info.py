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
