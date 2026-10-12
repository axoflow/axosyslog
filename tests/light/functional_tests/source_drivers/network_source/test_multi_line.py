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
import socket

import pytest

# no-multi-line: merged newlines become spaces
TEMPLATE = r'"${MESSAGE}\n"'

INPUT_LINES = [
    "2026-10-09 15:31:50 ERROR first",
    "  at com.example.First.run(First.java:1)",
    "  at com.example.First.main(First.java:2)",
    "2026-10-09 15:31:51 INFO second",
    "2026-10-09 15:31:52 WARN third",
    "  at com.example.Third.run(Third.java:3)",
]


def _create_logpath(config, port_allocator, **source_options):
    source = config.create_network_source(ip="localhost", port=port_allocator(), flags="no-parse", **source_options)
    destination = config.create_file_destination(file_name="output.log", template=TEMPLATE, flags="no-multi-line")
    config.create_logpath(statements=[source, destination])
    return source, destination


def test_indented_lines_are_merged_over_tcp(config, syslog_ng, port_allocator):
    source, destination = _create_logpath(config, port_allocator, transport="tcp", multi_line_mode="indented")

    syslog_ng.start(config)
    source.write_logs(INPUT_LINES)

    assert destination.read_logs(counter=3) == [
        "2026-10-09 15:31:50 ERROR first   at com.example.First.run(First.java:1)   at com.example.First.main(First.java:2)",
        "2026-10-09 15:31:51 INFO second",
        "2026-10-09 15:31:52 WARN third   at com.example.Third.run(Third.java:3)",
    ]


def test_prefix_garbage_lines_are_merged_over_tcp(config, syslog_ng, port_allocator):
    source, destination = _create_logpath(
        config, port_allocator, transport="tcp",
        multi_line_mode="prefix-garbage", multi_line_prefix='"^[0-9]{4}-"',
    )

    syslog_ng.start(config)
    source.write_logs(INPUT_LINES)

    assert destination.read_logs(counter=3) == [
        "2026-10-09 15:31:50 ERROR first   at com.example.First.run(First.java:1)   at com.example.First.main(First.java:2)",
        "2026-10-09 15:31:51 INFO second",
        "2026-10-09 15:31:52 WARN third   at com.example.Third.run(Third.java:3)",
    ]


def test_multi_line_mode_is_rejected_with_udp(config, syslog_ng, port_allocator):
    _create_logpath(config, port_allocator, transport="udp", multi_line_mode="indented")

    with pytest.raises(Exception):
        syslog_ng.start(config)


def test_multi_line_timeout_flushes_the_pending_message_on_an_open_connection(config, syslog_ng, port_allocator):
    source, destination = _create_logpath(
        config, port_allocator, transport="tcp",
        multi_line_mode="indented", multi_line_timeout=1,
    )

    syslog_ng.start(config)

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect(("localhost", source.options["port"]))
    try:
        sock.sendall(b"2026-10-09 15:31:50 ERROR first\n  at com.example.First.run(First.java:1)\n")

        # nothing ends the message, the timeout does
        assert destination.read_log() == "2026-10-09 15:31:50 ERROR first   at com.example.First.run(First.java:1)"

        # a continuation line after the flush is a message of its own
        sock.sendall(b"  at com.example.First.main(First.java:2)\n")
        assert destination.read_log() == "  at com.example.First.main(First.java:2)"
    finally:
        sock.close()


def test_multi_line_timeout_is_rejected_without_a_mode(config, syslog_ng, port_allocator):
    _create_logpath(config, port_allocator, transport="tcp", multi_line_timeout=1)

    with pytest.raises(Exception):
        syslog_ng.start(config)
