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
import os
import typing
from pathlib import Path
from subprocess import DEVNULL
from subprocess import Popen as subprocess_Popen

from axosyslog_light.common.docker_operations import ensure_image_exists
from axosyslog_light.executors.process_executor import ProcessExecutor
from axosyslog_light.helpers.snmptrapd.snmptrapd_executor import SnmpTrapdExecutor
from psutil import Popen

SNMPTRAPD_IMAGE = "axosyslog-light-snmptrapd:latest"


class SnmpTrapdDockerExecutor(SnmpTrapdExecutor):
    def __init__(self, container_name: str) -> None:
        self.__container_name = container_name
        self.__image = SNMPTRAPD_IMAGE
        self.__proc = None

    def start(self, snmptrapd_options: typing.List, stdout_path: Path, stderr_path: Path) -> Popen:
        ensure_image_exists(self.__image)
        subprocess_Popen(["docker", "rm", "-f", self.__container_name], stdout=DEVNULL, stderr=DEVNULL).wait()

        cwd = Path.cwd().absolute()
        command = ["docker", "run", "--rm", "-i"]
        command += ["--entrypoint", "snmptrapd"]
        command += ["--name", self.__container_name]
        command += ["--workdir", str(cwd)]
        command += ["--user", f"{os.getuid()}:{os.getgid()}"]
        command += ["-e", f"PUID={os.getuid()}", "-e", f"PGID={os.getgid()}"]
        command += ["--network", "host"]
        command += ["-v", f"{cwd}:{cwd}"]
        command += [self.__image]
        command += snmptrapd_options

        self.__proc = ProcessExecutor().start(command, stdout_path, stderr_path)
        return self.__proc

    def stop(self) -> None:
        subprocess_Popen(["docker", "rm", "-f", self.__container_name], stdout=DEVNULL, stderr=DEVNULL).wait()
        self.__proc = None
