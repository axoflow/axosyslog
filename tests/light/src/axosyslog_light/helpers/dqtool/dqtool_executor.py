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

import typing
from abc import ABC
from abc import abstractmethod
from pathlib import Path

from psutil import Popen


class DqToolExecutor(ABC):
    DEFAULT_EXECUTOR: typing.Optional[DqToolExecutor] = None

    @staticmethod
    def set_default_executor(dqtool_executor: DqToolExecutor) -> None:
        DqToolExecutor.DEFAULT_EXECUTOR = dqtool_executor.copy()

    @staticmethod
    def get_default_executor() -> typing.Optional[DqToolExecutor]:
        if DqToolExecutor.DEFAULT_EXECUTOR is None:
            return None
        return DqToolExecutor.DEFAULT_EXECUTOR.copy()

    def copy(self) -> DqToolExecutor:
        return self._copy()

    def cat(self, disk_buffer_file: Path, stderr: Path, stdout: Path, instance_index: int) -> Popen:
        return self._cat(disk_buffer_file, stderr, stdout, instance_index)

    def info(self, disk_buffer_file: Path, stderr: Path, stdout: Path, instance_index: int) -> Popen:
        return self._info(disk_buffer_file, stderr, stdout, instance_index)

    @abstractmethod
    def _copy(self) -> DqToolExecutor:
        pass
