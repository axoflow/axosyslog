#!/usr/bin/env python
#############################################################################
# Copyright (c) 2020 One Identity
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
from axosyslog_light.helpers.loggen.loggen_executor import LoggenExecutor
from axosyslog_light.helpers.loggen.loggen_executor import LoggenStartParams
from psutil import Popen


class LoggenStats:
    def __init__(self, loggen_stderr_file: Path) -> None:
        loggen_stderr_last_line = loggen_stderr_file.read_text(encoding="utf8").splitlines()[-1].strip()
        self.msg_per_second = float(loggen_stderr_last_line.split("avg_rate=")[1].split(" msg/s")[0])
        self.stdev_rate = float(loggen_stderr_last_line.split("stdev_rate=")[1].split("%")[0].replace(",", "."))
        self.total_msg_count = int(loggen_stderr_last_line.split("count=")[1].split(",")[0])
        self.run_time = float(loggen_stderr_last_line.split("time=")[1].split(",")[0])
        self.avg_msg_size = int(loggen_stderr_last_line.split("avg_msg_size=")[1].split(",")[0])
        self.bandwidth = float(loggen_stderr_last_line.split("bandwidth=")[1].split(" KiB/s")[0])


class Loggen(object):
    @staticmethod
    def __get_new_instance_index():
        with get_session_data() as session_data:
            last_index = session_data.get("loggen_instance_index", 0)
            index = last_index + 1
            session_data["loggen_instance_index"] = index
        return index

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

    def is_running(self) -> bool:
        return self.executor.is_running()

    def get_loggen_stats(self) -> LoggenStats:
        if not self.loggen_stderr_path.exists():
            raise RuntimeError("Loggen stderr file does not exist")

        return LoggenStats(self.loggen_stderr_path)

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
