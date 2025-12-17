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

import os
from pathlib import Path
from subprocess import DEVNULL
from subprocess import Popen as subprocess_Popen

from axosyslog_light.executors.command_executor import CommandExecutor
from axosyslog_light.helpers.dqtool.dqtool_executor import DqToolExecutor
from psutil import Popen


class DqToolDockerExecutor(DqToolExecutor):
    def __init__(self, image_name: str) -> None:
        self.__image_name = image_name
        super().__init__()

    def _cat(self, disk_buffer_file: Path, stderr: Path, stdout: Path, instance_index: int) -> Popen:
        subprocess_Popen(["docker", "rm", "-f", f"dqtool_{instance_index}"], stdout=DEVNULL, stderr=DEVNULL).wait()

        command = ["docker", "run", "--rm", "-i"]
        command += ["--entrypoint", "dqtool"]
        command += ["--name", f"dqtool_{instance_index}"]
        command += ["--workdir", str(Path.cwd().absolute())]
        command += ["--user", f"{os.getuid()}:{os.getgid()}"]
        command += ["-e", f"PUID={os.getuid()}", "-e", f"PGID={os.getgid()}"]
        command += ["--network", "host"]

        command += [self.__image_name]
        command += ["cat ", str(disk_buffer_file)]

        return CommandExecutor().run(command, stdout, stderr)

    def _info(self, disk_buffer_file: Path, stderr: Path, stdout: Path, instance_index: int) -> Popen:
        subprocess_Popen(["docker", "rm", "-f", f"dqtool_{instance_index}"], stdout=DEVNULL, stderr=DEVNULL).wait()

        command = ["docker", "run", "--rm", "-i"]
        command += ["--entrypoint", "dqtool"]
        command += ["--name", f"dqtool_{instance_index}"]
        command += ["--workdir", str(Path.cwd().absolute())]
        command += ["--user", f"{os.getuid()}:{os.getgid()}"]
        command += ["-e", f"PUID={os.getuid()}", "-e", f"PGID={os.getgid()}"]
        command += ["--network", "host"]

        command += [self.__image_name]
        command += ["info ", str(disk_buffer_file)]

        return CommandExecutor().run(command, stdout, stderr)

    def _copy(self) -> DqToolExecutor:
        return DqToolDockerExecutor(self.__image_name)
