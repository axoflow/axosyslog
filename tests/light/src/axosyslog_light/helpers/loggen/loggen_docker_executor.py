#!/usr/bin/env python
#############################################################################
# Copyright (c) 2025 Axoflow
# Copyright (c) 2025 Attila Szakacs <attila.szakacs@axoflow.com>
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
from __future__ import annotations

import os
import typing
from pathlib import Path

from axosyslog_light.executors.process_executor import ProcessExecutor
from axosyslog_light.helpers.loggen.loggen_executor import LoggenExecutor
from axosyslog_light.helpers.loggen.loggen_executor import LoggenStartParams
from psutil import Popen


class LoggenDockerExecutor(LoggenExecutor):
    def __init__(self, image_name: str) -> None:
        self.__image_name = image_name
        super().__init__()

    def _start(self, start_params: LoggenStartParams, stderr: Path, stdout: Path, instance_index: int) -> Popen:
        paths: typing.Set[Path] = {
            Path.cwd().absolute(),
        }
        if start_params.read_file:
            paths.add(Path(start_params.read_file).parent.absolute())

        command = ["docker", "run", "--rm", "-i"]
        command += ["--entrypoint", "loggen"]
        command += ["--name", f"loggen_{instance_index}"]
        command += ["--workdir", str(Path.cwd().absolute())]
        command += ["--user", f"{os.getuid()}:{os.getgid()}"]
        command += ["-e", f"PUID={os.getuid()}", "-e", f"PGID={os.getgid()}"]
        command += ["--network", "host"]

        for path in paths:
            command += ["-v", f"{path}:{path}"]

        command += [self.__image_name]
        command += start_params.format()

        return ProcessExecutor().start(command, stdout, stderr)

    def _copy(self) -> LoggenExecutor:
        return LoggenDockerExecutor(self.__image_name)
