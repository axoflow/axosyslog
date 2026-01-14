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
import re

from axosyslog_light.common.file import File

logger = logging.getLogger(__name__)


class ConsoleLogReader(object):
    def __init__(self, instance_paths, teardown):

        self.console_log_files = {
            "stderr": {"path": instance_paths.get_stderr_path(), "file": File(instance_paths.get_stderr_path())},
            "stdout": {"path": instance_paths.get_stdout_path(), "file": File(instance_paths.get_stdout_path())},
            "syntax_only_stderr": {"path": instance_paths.get_syntax_only_stderr_path(), "file": File(instance_paths.get_syntax_only_stderr_path())},
            "syntax_only_stdout": {"path": instance_paths.get_syntax_only_stdout_path(), "file": File(instance_paths.get_syntax_only_stdout_path())},
        }
        self.__teardown = teardown

    def wait_for_start_message(self):
        syslog_ng_start_message = ["syslog-ng starting up;"]
        return self.wait_for_messages_in_console_log(syslog_ng_start_message)

    def wait_for_stop_message(self):
        syslog_ng_stop_message = ["syslog-ng shutting down"]
        return self.wait_for_messages_in_console_log(syslog_ng_stop_message)

    def wait_for_reload_message(self):
        syslog_ng_reload_messages = [
            "New configuration initialized",
            "Configuration reload request received, reloading configuration",
            "Configuration reload finished",
        ]
        return self.wait_for_messages_in_console_log(syslog_ng_reload_messages)

    def wait_for_message_in_console_log(self, expected_message):
        return self.wait_for_messages_in_console_log(self, [expected_message])

    def wait_for_messages_in_console_log(self, expected_messages):
        stderr_file = self.console_log_files["stderr"]["file"]
        if not stderr_file.is_opened():
            stderr_file.wait_for_creation()
            stderr_file.open("r")
            self.__teardown.register(stderr_file.close)
        return stderr_file.wait_for_lines(expected_messages)

    def is_message_in_console_log(self, expected_message):
        console_logs = self.__get_all_console_logs()
        for console_log_message in console_logs.split("\n"):
            if expected_message in console_log_message:
                return True
        return False

    def check_for_unexpected_messages(self, unexpected_messages=None):
        unexpected_patterns = ["Plugin module not found", "assertion failed"]
        if unexpected_messages is not None:
            unexpected_patterns.extend(unexpected_messages)

        console_logs = self.__get_all_console_logs()
        for unexpected_pattern in unexpected_patterns:
            for console_log_message in console_logs.split("\n"):
                if re.search(unexpected_pattern, console_log_message) is not None:
                    logger.error("Found unexpected message in console log: {}".format(console_log_message))
                    raise Exception("Unexpected error log in console", console_log_message)

    def dump_stderr(self, last_n_lines=10):
        stderr = self.__get_all_console_logs()
        logger.error("\n".join(stderr.split("\n")[-last_n_lines:]))

    def __get_all_console_logs(self):
        console_logs = ""
        for log_type, log_path in self.console_log_files.items():
            if not log_path["path"].exists():
                logger.warning("Console log file {} does not exist".format(log_path))
                continue
            console_log_file = File(log_path["path"])
            console_log_file.open("r")
            console_logs += console_log_file.read()
            console_log_file.close()
        return console_logs

    @staticmethod
    def handle_valgrind_log(valgrind_log_path):
        with open(valgrind_log_path, "r") as valgrind_log:
            valgrind_content = valgrind_log.read()
            assert "Invalid read" not in valgrind_content
            assert "Invalid write" not in valgrind_content
            assert "blocks are definitely lost in loss record" not in valgrind_content
            assert "Uninitialised value was created by a heap allocation" not in valgrind_content
