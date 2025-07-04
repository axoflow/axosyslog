#!/usr/bin/env python
#############################################################################
# Copyright (c) 2015-2018 Balabit
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
from axosyslog_light.executors.process_executor import ProcessExecutor


def test_start_stop_process(tmpdir):
    stdout_file = tmpdir.join("stdout.log")
    stderr_file = tmpdir.join("stderr.log")
    process_executor = ProcessExecutor()
    process_command = ["ls"]
    process_executor.start(process_command, stdout_file, stderr_file)
    assert process_executor.process is not None
    assert process_executor.process.pid is not None
