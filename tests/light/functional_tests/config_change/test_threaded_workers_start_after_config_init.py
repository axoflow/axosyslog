#!/usr/bin/env python
#############################################################################
# Copyright (c) 2026 Axoflow
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
from pathlib import Path

from axosyslog_light.common.blocking import wait_until_true

WORKER_STARTED = "Worker thread started"


def _console_log_lines(syslog_ng):
    return Path(syslog_ng.instance_paths.get_stderr_path()).read_text().splitlines()


def _indexes_of(lines, needle):
    return [index for index, line in enumerate(lines) if needle in line]


def _wait_for_worker_starts(syslog_ng, count):
    assert wait_until_true(lambda: len(_indexes_of(_console_log_lines(syslog_ng), WORKER_STARTED)) >= count)
    return _console_log_lines(syslog_ng)


def test_threaded_workers_start_after_config_init(config, syslog_ng):
    # A threaded source processes messages as soon as its worker thread is
    # started, so the thread must only be started once the configuration is
    # fully in place: after the startup notice at startup and after the new
    # configuration has been initialized and switched to at reload.
    # example_random_generator() runs in a dedicated thread (unlike
    # example_msg_generator(), which is a main loop timer).
    generator_source = config.create_example_random_generator_source(freq=0.1, bytes=16)
    file_destination = config.create_file_destination(file_name="output.log")
    config.create_logpath(statements=[generator_source, file_destination])

    syslog_ng.start(config)
    lines = _wait_for_worker_starts(syslog_ng, 1)
    assert _indexes_of(lines, "syslog-ng starting up")[0] < _indexes_of(lines, WORKER_STARTED)[0]

    syslog_ng.reload(config)
    lines = _wait_for_worker_starts(syslog_ng, 2)
    assert _indexes_of(lines, "New configuration initialized")[0] < _indexes_of(lines, WORKER_STARTED)[1]
