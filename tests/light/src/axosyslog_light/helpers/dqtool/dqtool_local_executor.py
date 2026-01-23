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
from __future__ import annotations

from pathlib import Path

from axosyslog_light.executors.command_executor import CommandExecutor
from axosyslog_light.helpers.dqtool.dqtool_executor import DqToolExecutor
from psutil import Popen


class DqToolLocalExecutor(DqToolExecutor):
    def __init__(self, dqtool_binary_path: Path) -> None:
        self.__binary_path = dqtool_binary_path
        super().__init__()

    def _cat(self, disk_buffer_file: Path, stderr: Path, stdout: Path, instance_index: int) -> Popen:
        dqtool_cat_cmd = [self.__binary_path, "cat", disk_buffer_file]
        return CommandExecutor().run(dqtool_cat_cmd, stdout, stderr)

    def _info(self, disk_buffer_file: Path, stderr: Path, stdout: Path, instance_index: int) -> Popen:
        dqtool_info_cmd = [self.__binary_path, "info", disk_buffer_file]
        return CommandExecutor().run(dqtool_info_cmd, stdout, stderr)

    def _copy(self) -> DqToolExecutor:
        return DqToolLocalExecutor(self.__binary_path)
