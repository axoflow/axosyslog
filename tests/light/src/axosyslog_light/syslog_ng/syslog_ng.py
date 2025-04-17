#!/usr/bin/env python
#############################################################################
# Copyright (c) 2015-2018 Balabit
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
import logging
import typing
from copy import copy
from pathlib import Path
from subprocess import Popen

from axosyslog_light.common.blocking import wait_until_false
from axosyslog_light.common.blocking import wait_until_true
from axosyslog_light.common.random_id import get_unique_id
from axosyslog_light.syslog_ng.console_log_reader import ConsoleLogReader
from axosyslog_light.syslog_ng.syslog_ng_executor import SyslogNgExecutor
from axosyslog_light.syslog_ng.syslog_ng_executor import SyslogNgStartParams
from axosyslog_light.syslog_ng.syslog_ng_paths import SyslogNgPaths
from axosyslog_light.syslog_ng_config.syslog_ng_config import SyslogNgConfig
from axosyslog_light.syslog_ng_ctl.legacy_stats_handler import LegacyStatsHandler
from axosyslog_light.syslog_ng_ctl.prometheus_stats_handler import PrometheusStatsHandler
from axosyslog_light.syslog_ng_ctl.syslog_ng_ctl import SyslogNgCtl
from axosyslog_light.testcase_parameters.testcase_parameters import TestcaseParameters

logger = logging.getLogger(__name__)


class SyslogNg(object):
    def __init__(
        self,
        syslog_ng_executor: SyslogNgExecutor,
        syslog_ng_ctl: SyslogNgCtl,
        instance_paths: SyslogNgPaths,
        testcase_parameters: TestcaseParameters,
        teardown,
    ) -> None:
        self.instance_paths = instance_paths
        self.start_params = SyslogNgStartParams(
            config_path=self.instance_paths.get_config_path(),
            persist_path=self.instance_paths.get_persist_path(),
            pid_path=self.instance_paths.get_pid_path(),
            control_socket_path=self.instance_paths.get_control_socket_path(),
        )
        self._external_tool = testcase_parameters.get_external_tool()
        self._console_log_reader = ConsoleLogReader(self.instance_paths, teardown)
        self._syslog_ng_ctl = syslog_ng_ctl
        self._syslog_ng_executor = syslog_ng_executor
        self._process: typing.Optional[Popen] = None

    def create_config(self, version: str, teardown) -> SyslogNgConfig:
        return SyslogNgConfig(
            version,
            LegacyStatsHandler(self._syslog_ng_ctl),
            PrometheusStatsHandler(self._syslog_ng_ctl),
            teardown,
        )

    def start(self, config: SyslogNgConfig) -> Popen:
        if self._process:
            raise Exception("syslog-ng has been already started")

        config.write_config(self.instance_paths.get_config_path())

        self.__syntax_check()
        self.__start_syslog_ng()

        logger.info("syslog-ng process has been started with PID: {}\n".format(self._process.pid))

        return self._process

    def stop(self, unexpected_messages: typing.List[str] = None) -> None:
        if self.is_process_running():
            saved_pid = self._process.pid
            # effective stop
            result = self._syslog_ng_ctl.stop()

            # wait for stop and check stop result
            if result["exit_code"] != 0:
                self.__error_handling("Control socket fails to stop syslog-ng")
            if not wait_until_false(self.is_process_running):
                self.__error_handling("syslog-ng did not stop")
            if self.start_params.stderr and self.start_params.debug and self.start_params.verbose:
                if not self._console_log_reader.wait_for_stop_message():
                    self.__error_handling("Stop message not arrived")
            self._console_log_reader.check_for_unexpected_messages(unexpected_messages)
            if self._external_tool == "valgrind":
                self._console_log_reader.handle_valgrind_log(Path(f"syslog_ng_{self.instance_paths.get_instance_name()}_valgrind_output"))
            logger.info("syslog-ng process has been stopped with PID: {}\n".format(saved_pid))
        self._process = None

    def reload(self, config: SyslogNgConfig) -> None:
        config.write_config(self.instance_paths.get_config_path())

        # effective reload
        result = self._syslog_ng_ctl.reload()

        # wait for reload and check reload result
        if result["exit_code"] != 0:
            self.__error_handling("Control socket fails to reload syslog-ng")
        if not self.__wait_for_control_socket_alive():
            self.__error_handling("Control socket not alive")
        if self.start_params.stderr and self.start_params.debug and self.start_params.verbose:
            if not self._console_log_reader.wait_for_reload_message():
                self.__error_handling("Reload message not arrived")
        logger.info("syslog-ng process has been reloaded with PID: {}\n".format(self._process.pid))

    def restart(self, config: SyslogNgConfig) -> None:
        self.stop()
        self.start(config)

    def _get_version_info(self, parse_line: typing.Callable[[str], str], error_message: str) -> str:
        stdout_path = Path(f"syslog_ng_{self.instance_paths.get_instance_name()}_version_stdout")
        stderr_path = Path(f"syslog_ng_{self.instance_paths.get_instance_name()}_version_stderr")

        start_params = copy(self.start_params)
        start_params.version = True

        process = self._syslog_ng_executor.run_process(
            start_params=start_params,
            stderr_path=stderr_path,
            stdout_path=stdout_path,
        )
        returncode = process.wait()
        if returncode != 0:
            raise Exception(f"{error_message}. Process returned with {returncode}. See {stderr_path.absolute()} for details")

        for line in stdout_path.read_text().splitlines():
            try:
                return parse_line(line)
            except (IndexError, ValueError):
                continue
        raise Exception(f"Cannot parse version information from 'syslog-ng --version'")

    def get_config_version(self) -> str:
        def parse_line(line: str) -> str:
            if "Config version:" in line:
                return line.split()[2]
            raise ValueError
        return self._get_version_info(parse_line, "Cannot get syslog-ng config version")

    def get_version(self) -> str:
        def parse_line(line: str) -> str:
            return line.split()[2]
        return self._get_version_info(parse_line, "Cannot get syslog-ng version")

    def is_process_running(self) -> bool:
        return self._process and self._process.poll() is None

    def wait_for_messages_in_console_log(self, expected_messages: typing.List[str]) -> typing.List[str]:
        assert issubclass(type(expected_messages), list)
        return self._console_log_reader.wait_for_messages_in_console_log(expected_messages)

    def wait_for_message_in_console_log(self, expected_message: str) -> typing.List[str]:
        return self.wait_for_messages_in_console_log([expected_message])

    def __syntax_check(self) -> None:
        stdout_path = Path(f"syslog_ng_{self.instance_paths.get_instance_name()}_syntax_only_stdout")
        stderr_path = Path(f"syslog_ng_{self.instance_paths.get_instance_name()}_syntax_only_stderr")

        start_params = copy(self.start_params)
        start_params.syntax_only = True

        process = self._syslog_ng_executor.run_process(
            start_params=start_params,
            stderr_path=stderr_path,
            stdout_path=stdout_path,
        )
        returncode = process.wait()
        if returncode != 0:
            raise Exception(f"syslog-ng syntax error. See {stderr_path.absolute()} for details")

    def __wait_for_control_socket_alive(self) -> bool:
        def is_alive(s: SyslogNg) -> bool:
            if not s.is_process_running():
                self.__error_handling("syslog-ng is not running")
            return s._syslog_ng_ctl.is_control_socket_alive()
        return wait_until_true(is_alive, self)

    def __wait_for_start(self) -> None:
        # wait for start and check start result
        if not self.__wait_for_control_socket_alive():
            self.__error_handling("Control socket not alive")
        if not self._console_log_reader.wait_for_start_message():
            self.__error_handling("Start message not arrived")

    def __start_syslog_ng(self) -> None:
        args = {
            "start_params": self.start_params,
            "stderr_path": self.instance_paths.get_stderr_path(),
            "stdout_path": self.instance_paths.get_stdout_path(),
        }

        if self._external_tool is None:
            self._process = self._syslog_ng_executor.run_process(**args)
        elif self._external_tool == "gdb":
            self._process = self._syslog_ng_executor.run_process_with_gdb(**args)
        elif self._external_tool == "valgrind":
            self._process = self._syslog_ng_executor.run_process_with_valgrind(
                valgrind_output_path=Path(f"syslog_ng_{self.instance_paths.get_instance_name()}_valgrind_output"),
                **args,
            )
        elif self._external_tool == "strace":
            self._process = self._syslog_ng_executor.run_process_with_strace(
                strace_output_path=Path(f"syslog_ng_{self.instance_paths.get_instance_name()}_strace_output"),
                **args,
            )
        else:
            raise Exception(f"Unknown external tool: {self._external_tool}")

        if self.start_params.stderr and self.start_params.debug and self.start_params.verbose:
            if self.start_params.preprocess_into is None and not self.start_params.syntax_only:
                self.__wait_for_start()
            else:
                self._process.wait()

    def __error_handling(self, error_message: str) -> typing.NoReturn:
        self._console_log_reader.dump_stderr()
        self.__handle_core_file()
        raise Exception(error_message)

    def __handle_core_file(self) -> None:
        if not self.is_process_running():
            core_file_found = False
            for core_file in Path(".").glob("*core*"):
                core_file_found = True
                self._process = None

                core_postfix = "gdb_core_{}".format(get_unique_id())
                stderr_path = self.instance_paths.get_stderr_path_with_postfix(core_postfix)
                stdout_path = self.instance_paths.get_stdout_path_with_postfix(core_postfix)

                self._syslog_ng_executor.get_backtrace_from_core(
                    core_file,
                    stderr_path,
                    stdout_path,
                )
                core_file.replace(Path(core_file))
            if core_file_found:
                raise Exception("syslog-ng core file was found and processed")
            if self._process.returncode in [-6, -9, -11]:
                ret_code = self._process.returncode
                self._process = None
                raise Exception("syslog-ng process crashed with signal {}".format(ret_code))
