#!/usr/bin/env python
#############################################################################
# Copyright (c) 2020 Balabit
# Copyright (c) 2020 Nobles
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

DEFAULT_PID = "pid"


@pytest.mark.parametrize(
    "use_syslogng_pid", [("yes", "no")],
)
def test_use_syslogng_pid(config, syslog_ng, use_syslogng_pid):
    generator_source = config.create_example_msg_generator_source(num=1, use_syslogng_pid=use_syslogng_pid, values="PID => {}".format(DEFAULT_PID))
    file_destination = config.create_file_destination(file_name="output.log", template=config.stringify("PID=$PID\n"))
    config.create_logpath(statements=[generator_source, file_destination])

    p = syslog_ng.start(config)
    if use_syslogng_pid == "yes":
        expected_value = "PID={}".format(p.pid)
    else:
        expected_value = "PID={}".format(DEFAULT_PID)
    assert file_destination.read_log().strip() == expected_value
