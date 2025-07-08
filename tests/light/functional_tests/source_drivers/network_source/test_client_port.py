#!/usr/bin/env python
#############################################################################
# Copyright (c) 2025 Axoflow
# Copyright (c) 2025 Attila Szakacs <attila.szakacs@axoflow.com>
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
import re


def test_client_port(config, syslog_ng, port_allocator):
    server_port = port_allocator()
    client_port = port_allocator()

    source = config.create_network_source(port=server_port)

    file_destination = config.create_file_destination(file_name="output.txt")
    config.create_logpath(statements=[source, file_destination])

    syslog_ng.start(config)

    source.write_log("almafa", client_port=client_port)

    possible_client_connection_logs = syslog_ng.wait_for_message_in_console_log(":" + str(client_port))
    pattern = r"Syslog connection accepted; fd='\d+', client='AF_INET\(127\.0\.0\.1:" + str(client_port) + r"\)', local='AF_INET\(0\.0\.0\.0:" + str(server_port) + r"\)'"
    assert any(re.search(pattern, log) for log in possible_client_connection_logs)

    syslog_ng.stop()
