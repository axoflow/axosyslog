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
import logging
import os
from pathlib import Path
from subprocess import DEVNULL
from subprocess import Popen

from axosyslog_light.common.blocking import wait_until_true
from axosyslog_light.executors.process_executor import ProcessExecutor
from axosyslog_light.helpers.clickhouse.clickhouse_server_executor import ClickhouseServerExecutor
from axosyslog_light.helpers.clickhouse.clickhouse_server_executor import CONFIG_FILE

logger = logging.getLogger(__name__)

CLICKHOUSE_IMAGE = "clickhouse/clickhouse-server:latest"


class ClickhouseServerDockerExecutor(ClickhouseServerExecutor):
    def __init__(self, testcase_parameters, container_name: str) -> None:
        super().__init__(testcase_parameters)
        self.__container_name = container_name
        self.__image = CLICKHOUSE_IMAGE

    def _start_server(self):
        Popen(["docker", "rm", "-f", self.__container_name], stdout=DEVNULL, stderr=DEVNULL).wait()

        cwd = Path.cwd().absolute()
        command = ["docker", "run", "--rm", "-i"]
        command += ["--entrypoint", "clickhouse-server"]
        command += ["--name", self.__container_name]
        command += ["--workdir", str(cwd)]
        command += ["--user", f"{os.getuid()}:{os.getgid()}"]
        command += ["-e", f"PUID={os.getuid()}", "-e", f"PGID={os.getgid()}"]
        command += ["--network", "host"]
        command += ["-v", f"{cwd}:{cwd}"]
        command += [self.__image]
        command += ["--config-file", CONFIG_FILE]

        return ProcessExecutor().start(command, "clickhouse_server.stdout", "clickhouse_server.stderr")

    def stop(self) -> None:
        Popen(["docker", "rm", "-f", self.__container_name], stdout=DEVNULL, stderr=DEVNULL).wait()
        wait_until_true(lambda: not self.process.is_running())
        logger.info("Clickhouse server container %s removed.", self.__container_name)
