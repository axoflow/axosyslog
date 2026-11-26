#!/usr/bin/env python
#############################################################################
# Copyright (c) 2026 Axoflow
# Copyright (c) 2026 Balazs Scheidler <balazs.scheidler@axoflow.com>
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
import pytest

TEMPLATE = r'"${MESSAGE}\n"'


def test_multi_line_timeout_flushes_the_pending_message(config, syslog_ng):
    timeout = 1
    file_source = config.create_file_source(
        file_name="input.log", flags="no-parse", follow_freq=timeout,
        multi_line_mode="indented", multi_line_timeout=timeout,
    )
    file_destination = config.create_file_destination(file_name="output.log", template=TEMPLATE, flags="no-multi-line")
    config.create_logpath(statements=[file_source, file_destination])

    syslog_ng.start(config)
    file_source.write_logs(["2026-10-09 15:31:50 ERROR first", "  at com.example.First.run(First.java:1)"])

    # nothing ends the message, the timeout does
    assert file_destination.read_log() == "2026-10-09 15:31:50 ERROR first   at com.example.First.run(First.java:1)"

    # a continuation line after the flush is a message of its own
    file_source.write_log("  at com.example.First.main(First.java:2)")
    assert file_destination.read_log() == "  at com.example.First.main(First.java:2)"


def test_multi_line_timeout_requires_a_mode(config, syslog_ng):
    file_source = config.create_file_source(file_name="input.log", multi_line_timeout=1)
    file_destination = config.create_file_destination(file_name="output.log")
    config.create_logpath(statements=[file_source, file_destination])

    with pytest.raises(Exception):
        syslog_ng.start(config)
