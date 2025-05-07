#!/usr/bin/env python
#############################################################################
# Copyright (c) 2025 Axoflow
# Copyright (c) 2025 László Várady
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as published
# by the Free Software Foundation, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
#
# As an additional exemption you are allowed to compile & link against the
# OpenSSL libraries as published by the OpenSSL project. See the file
# COPYING for details.
#
#############################################################################
import os


def test_filterx_cache_json_file_reloads_its_content_automatically_on_write_close(syslog_ng, config):
    cache_json_file_path = "./c.json"

    source = config.create_example_msg_generator_source(num=10)
    filterx = config.create_filterx(f"""
        cached = cache_json_file("{cache_json_file_path}");
        $MSG = cached.msg;
""")
    destination = config.create_file_destination(file_name="output.log", template='"$MSG\n"')

    config.create_logpath(statements=[source, filterx, destination])

    with open(cache_json_file_path, "w") as file:
        file.write('{"msg": "orig"}')

    syslog_ng.start(config)
    assert destination.read_until_logs(["orig"])

    with open(cache_json_file_path, "w") as file:
        file.write('{"msg": "autoupdated"}')

    assert destination.read_until_logs(["autoupdated"])


def test_filterx_cache_json_file_reloads_its_content_automatically_on_atomic_write(syslog_ng, config):
    cache_json_file_path = "./c.json"
    cache_json_file_path_tmp = "./c.json.tmp"

    source = config.create_example_msg_generator_source(num=10)
    filterx = config.create_filterx(f"""
        cached = cache_json_file("{cache_json_file_path}");
        $MSG = cached.msg;
""")
    destination = config.create_file_destination(file_name="output.log", template='"$MSG\n"')

    config.create_logpath(statements=[source, filterx, destination])

    with open(cache_json_file_path, "w") as file:
        file.write('{"msg": "orig"}')

    syslog_ng.start(config)
    assert destination.read_until_logs(["orig"])

    with open(cache_json_file_path_tmp, "w") as file:
        file.write('{"msg": "atomic write"}')

    os.rename(cache_json_file_path_tmp, cache_json_file_path)

    assert destination.read_until_logs(["atomic write"])
