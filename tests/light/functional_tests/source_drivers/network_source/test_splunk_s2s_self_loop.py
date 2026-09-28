#!/usr/bin/env python
#############################################################################
# Copyright (c) 2026 Adam Kiss
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


def test_splunk_s2s_self_loop(config, syslog_ng, port_allocator):
    counter = 100
    port = port_allocator()
    message = "splunk-s2s self-loop payload"

    generator = config.create_example_msg_generator_source(
        num=counter,
        freq=0.0001,
        template=config.stringify(message),
    )
    metadata = [
        config.create_rewrite_set(config.stringify("test-index"), value=config.stringify(".splunk.index")),
        config.create_rewrite_set(config.stringify("test-source"), value=config.stringify(".splunk.source")),
        config.create_rewrite_set(config.stringify("test-sourcetype"), value=config.stringify(".splunk.sourcetype")),
        config.create_rewrite_set(config.stringify("test-host"), value=config.stringify(".splunk.host")),
    ]
    s2s_destination = config.create_network_destination(
        ip="localhost",
        port=port,
        transport=config.stringify("splunk-s2s"),
        template=config.stringify("$MSG"),
        time_reopen=1,
    )
    config.create_logpath(statements=[generator, *metadata, s2s_destination])

    s2s_source = config.create_splunk_s2s_source(port=port, flags="no-parse")
    file_destination = config.create_file_destination(
        file_name="output.log",
        template=r'"${.splunk.index}|${.splunk.source}|${.splunk.sourcetype}|${.splunk.host}|${MESSAGE}\n"',
    )
    config.create_logpath(statements=[s2s_source, file_destination])

    syslog_ng.start(config)

    expected = "test-index|test-source|test-sourcetype|test-host|" + message
    assert file_destination.read_until_logs([expected] * counter)
