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
import typing
from pathlib import Path

from axosyslog_light.executors.process_executor import ProcessExecutor
from axosyslog_light.helpers.snmptrapd.snmptrapd_executor import SnmpTrapdExecutor
from psutil import Popen
from psutil import TimeoutExpired


class SnmpTrapdLocalExecutor(SnmpTrapdExecutor):
    def __init__(self) -> None:
        self.__proc = None

    def start(self, snmptrapd_options: typing.List, stdout_path: Path, stderr_path: Path) -> Popen:
        self.__proc = ProcessExecutor().start(["snmptrapd"] + snmptrapd_options, stdout_path, stderr_path)
        return self.__proc

    def stop(self) -> None:
        if self.__proc is None:
            return

        self.__proc.terminate()
        try:
            self.__proc.wait(4)
        except TimeoutExpired:
            self.__proc.kill()

        self.__proc = None
