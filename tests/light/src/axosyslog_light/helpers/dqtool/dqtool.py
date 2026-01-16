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

from axosyslog_light.common.session_data import get_session_data
from axosyslog_light.helpers.dqtool.dqtool_executor import DqToolExecutor
from psutil import Popen


class DqTool():
    @staticmethod
    def __get_new_instance_index():
        with get_session_data() as session_data:
            last_index = session_data.get("loggen_instance_index", 0)
            index = last_index + 1
            session_data["loggen_instance_index"] = index
        return index

    def __init__(self, dqtool_executor: typing.Optional[DqToolExecutor] = None):
        if dqtool_executor is None:
            dqtool_executor = DqToolExecutor.get_default_executor()

        if not dqtool_executor:
            raise Exception("DqTool executor is not available")

        self.executor = dqtool_executor

    def cat(self, disk_buffer_file: Path) -> Popen:
        instance_index = DqTool.__get_new_instance_index()
        self.dqtool_stdout_path = Path("dqtool_cat_stdout_{}".format(instance_index))
        self.dqtool_stderr_path = Path("dqtool_cat_stderr_{}".format(instance_index))
        return self.executor.cat(disk_buffer_file, self.dqtool_stderr_path, self.dqtool_stdout_path, instance_index)

    def info(self, disk_buffer_file: Path) -> Popen:
        instance_index = DqTool.__get_new_instance_index()
        self.dqtool_stdout_path = Path("dqtool_info_stdout_{}".format(instance_index))
        self.dqtool_stderr_path = Path("dqtool_info_stderr_{}".format(instance_index))
        return self.executor._info(disk_buffer_file, self.dqtool_stderr_path, self.dqtool_stdout_path, instance_index)
