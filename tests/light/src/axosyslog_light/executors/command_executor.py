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

import psutil
from axosyslog_light.common.file import File

logger = logging.getLogger(__name__)


def prepare_std_outputs(stdout_path, stderr_path):
    stdout = File(stdout_path)
    stderr = File(stderr_path)
    return stdout, stderr


def prepare_printable_command(command):
    printable_command = ""
    for command_arg in command:
        printable_command += "{} ".format(str(command_arg))
    return printable_command


def prepare_executable_command(command):
    executable_command = []
    for command_arg in command:
        executable_command.append(str(command_arg))
    return executable_command


class CommandExecutor(object):
    def __init__(self):
        self.__start_timeout = 10

    def run(self, command, stdout_path, stderr_path):
        printable_command = prepare_printable_command(command)
        executable_command = prepare_executable_command(command)
        stdout, stderr = prepare_std_outputs(stdout_path, stderr_path)
        logger.debug("The following command will be executed:\n{}\n".format(printable_command))
        cmd = psutil.Popen(executable_command, stdout=stdout.open(mode="w"), stderr=stderr.open(mode="w"))
        exit_code = cmd.wait(timeout=self.__start_timeout)

        stdout.close()
        stderr.close()
        stdout_content, stderr_content = self.__process_std_outputs(stdout, stderr)
        self.__process_exit_code(printable_command, exit_code, stdout_content, stderr_content)
        return {"exit_code": exit_code, "stdout": stdout_content, "stderr": stderr_content}

    def __process_std_outputs(self, stdout, stderr):
        stdout.open("r")
        stdout_content = stdout.read()
        stdout.close()

        stderr.open("r")
        stderr_content = stderr.read()
        stderr.close()

        return stdout_content, stderr_content

    def __process_exit_code(self, command, exit_code, stdout, stderr):
        exit_code_debug_log = "\nExecuted command: {}\n\
Exit code: {}\n\
Stdout: {}\n\
Stderr: {}\n".format(command.rstrip(), exit_code, stdout.rstrip(), stderr.rstrip())
        logger.debug(exit_code_debug_log)
