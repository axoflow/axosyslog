#!/usr/bin/env python
#############################################################################
# Copyright (c) 2020 One Identity
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
import typing
from pathlib import Path

from axosyslog_light.helpers.loggen.loggen_executor import LoggenExecutor
from axosyslog_light.helpers.loggen.loggen_executor import LoggenStartParams
from psutil import Popen


class Loggen(object):
    instanceIndex = -1

    @staticmethod
    def __get_new_instance_index():
        Loggen.instanceIndex += 1
        return Loggen.instanceIndex

    def __init__(self, loggen_executor: typing.Optional[LoggenExecutor] = None):
        if loggen_executor is None:
            loggen_executor = LoggenExecutor.get_default_executor()

        if not loggen_executor:
            raise Exception("Loggen executor is not available")

        self.executor = loggen_executor

    def start(self, start_params: LoggenStartParams) -> Popen:
        instance_index = Loggen.__get_new_instance_index()
        self.loggen_stdout_path = Path("loggen_stdout_{}".format(instance_index))
        self.loggen_stderr_path = Path("loggen_stderr_{}".format(instance_index))
        return self.executor.start(start_params, self.loggen_stderr_path, self.loggen_stdout_path, instance_index)

    def stop(self) -> None:
        self.executor.stop()

    def get_sent_message_count(self) -> int:
        if not self.loggen_stderr_path.exists():
            return 0

        # loggen puts the count= messages to the stderr
        f = open(self.loggen_stderr_path, "r")
        content = f.read()
        f.close()

        start_pattern = "count="
        if start_pattern not in content:
            return 0
        index_start = content.rindex(start_pattern) + len(start_pattern)

        end_pattern = ", "
        if end_pattern not in content:
            return 0
        index_end = content.find(end_pattern, index_start)

        return int(content[index_start:index_end])
