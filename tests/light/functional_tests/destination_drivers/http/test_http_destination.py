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


def test_http_destination_accepts_messages(config, syslog_ng, port_allocator):
    port = port_allocator()
    num_messages = 5

    generator_source = config.create_example_msg_generator_source(
        num=num_messages,
        template=config.stringify("test message"),
    )
    http_destination = config.create_http_destination(
        port=port,
        body=config.stringify("${MSG} ${UNIQID}"),
    )
    config.create_logpath(statements=[generator_source, http_destination])

    syslog_ng.start(config)

    messages = http_destination.read_logs(num_messages)
    assert len(messages) == num_messages
    assert all("test message" in msg for msg in messages)
