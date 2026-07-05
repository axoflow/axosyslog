#!/usr/bin/env python
#############################################################################
# Copyright (c) 2026 Axoflow
# Copyright (c) 2026 Andras Mitzki <andras.mitzki@axoflow.com>
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
from pathlib import Path

from axosyslog_light.common.file import copy_shared_file
from axosyslog_light.driver_io.file.file_io import FileIO


def test_filterx_pipeline_smoke(config, syslog_ng, testcase_parameters, loggen, syslog_ng_ctl):
    copy_shared_file(testcase_parameters, "callgrind-syslog-ng.conf")
    copy_shared_file(testcase_parameters, "callgrind-samples.log")
    config_content = Path("callgrind-syslog-ng.conf").read_text()
    config.set_raw_config(config_content)

    # JIT compiling this config under ASan can take longer than the default timeout
    syslog_ng.start(config, startup_timeout=40)
    network_source = config.create_network_source(ip="localhost", port=2000)
    logs = FileIO("callgrind-samples.log").read_all()
    network_source.write_logs(logs)

    file_destination = config.create_file_destination(file_name="output.txt")
    assert len(file_destination.read_logs(len(logs))) == len(logs)

    syslog_ng.stop()
