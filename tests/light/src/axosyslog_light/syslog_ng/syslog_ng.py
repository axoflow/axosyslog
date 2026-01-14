#!/usr/bin/env python
#############################################################################
# Copyright (c) 2015-2018 Balabit
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
import typing
from copy import copy
from pathlib import Path
from subprocess import Popen

from axosyslog_light.common.blocking import wait_until_false
from axosyslog_light.common.blocking import wait_until_true
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

    def create_config(self, config_version: str, teardown) -> SyslogNgConfig:
        return SyslogNgConfig(
            config_version,
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
            self.__validate_returncode(result["exit_code"])
            if not wait_until_false(self.is_process_running):
                self.__error_handling("syslog-ng did not stop")
            if self.start_params.stderr and self.start_params.debug and self.start_params.verbose:
                if not self._console_log_reader.wait_for_stop_message():
                    logger.warning("Stop message has not been found, this might be because of a very long console log")
            self._console_log_reader.check_for_unexpected_messages(unexpected_messages)
            logger.info("syslog-ng process has been stopped with PID: {}\n".format(saved_pid))
        else:
            if self._process is not None:
                self.__validate_returncode(self._process.returncode)
            self._console_log_reader.check_for_unexpected_messages(unexpected_messages)
        self._process = None

    def reload(self, config: SyslogNgConfig) -> None:
        config.write_config(self.instance_paths.get_config_path())

        # effective reload
        result = self._syslog_ng_ctl.reload()

        # wait for reload and check reload result
        self.__validate_returncode(result["exit_code"])
        if not self.__wait_for_control_socket_alive():
            self.__error_handling("Control socket not alive")
        if self.start_params.stderr and self.start_params.debug and self.start_params.verbose:
            if not self._console_log_reader.wait_for_reload_message():
                self.__error_handling("Reload message not arrived")
        logger.info("syslog-ng process has been reloaded with PID: {}\n".format(self._process.pid))

    def restart(self, config: SyslogNgConfig) -> None:
        self.stop()
        self.start(config)

    def _get_version_info(self, keyword: str) -> str:
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
            raise Exception(f"Cannot get syslog-ng {keyword.lower()}. Process returned with {returncode}. See {stderr_path.absolute()} for details")

        for line in stdout_path.read_text().splitlines():
            if keyword in line:
                return line.split(":")[1].lstrip()
        raise Exception(f"Cannot parse '{keyword}' from 'syslog-ng --version'")

    def get_config_version(self) -> str:
        return self._get_version_info("Config version:")

    def get_version(self) -> str:
        return self._get_version_info("Installer-Version:")

    def is_process_running(self) -> bool:
        return self._process and self._process.poll() is None

    def wait_for_messages_in_console_log(self, expected_messages: typing.List[str]) -> typing.List[str]:
        assert issubclass(type(expected_messages), list)
        return self._console_log_reader.wait_for_messages_in_console_log(expected_messages)

    def wait_for_message_in_console_log(self, expected_message: str) -> typing.List[str]:
        return self.wait_for_messages_in_console_log([expected_message])

    def is_message_in_console_log(self, expected_message: str) -> bool:
        return self._console_log_reader.is_message_in_console_log(expected_message)

    def __syntax_check(self) -> None:
        stdout_path = Path(self.instance_paths.get_syntax_only_stdout_path())
        stderr_path = Path(self.instance_paths.get_syntax_only_stderr_path())

        start_params = copy(self.start_params)
        start_params.syntax_only = True

        process = self._syslog_ng_executor.run_process(
            start_params=start_params,
            stderr_path=stderr_path,
            stdout_path=stdout_path,
        )
        returncode = process.wait()
        if returncode == 0:
            logger.info("syslog-ng config syntax check passed")
        elif returncode == 1:
            raise Exception(f"syslog-ng config syntax error. See {stderr_path.absolute()} for details")
        else:
            self.__validate_returncode(returncode)

    def __wait_for_control_socket_alive(self) -> bool:
        def is_alive(s: SyslogNg) -> bool:
            if not s.is_process_running():
                self.__validate_returncode(self._process.returncode)
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
        elif self._external_tool == "gdb_for_bt":
            self._process = self._syslog_ng_executor.run_process_with_gdb_for_bt(**args)
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

        if self.start_params.stderr:
            if self.start_params.preprocess_into is None and not self.start_params.syntax_only:
                self.__wait_for_start()
            else:
                self._process.wait()

    def __error_handling(self, error_message: str) -> typing.NoReturn:
        self._console_log_reader.dump_stderr()
        raise Exception(error_message)

    def __validate_returncode(self, returncode: int):
        if returncode not in [0, 1, 2]:
            # return code 1 is a directed way of termination (syntax error), it should not handle as a crash
            # return code 2 is a directed way of termination, it should not handle as a crash
            self._process = None
            assert False, "syslog-ng has crashed with return code: %s" % returncode
