#!/usr/bin/env python
#############################################################################
# Copyright (c) 2020 One Identity
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

TEMPLATE = r'"${SOURCEIP} ${SOURCEPORT} ${DESTIP} ${DESTPORT} ${IP_PROTO} ${MESSAGE}\n"'

PROXY_VERSION = 1

CLIENT_A_PROXY_SRC_IP = "1.1.1.1"
CLIENT_A_PROXY_DST_IP = "2.2.2.2"
CLIENT_A_PROXY_SRC_PORT = 3333
CLIENT_A_PROXY_DST_PORT = 4444
CLIENT_A_INPUT = ["message A 0", "message A 1"]

CLIENT_B_PROXY_SRC_IP = "5.5.5.5"
CLIENT_B_PROXY_DST_IP = "6.6.6.6"
CLIENT_B_PROXY_SRC_PORT = 7777
CLIENT_B_PROXY_DST_PORT = 8888
CLIENT_B_INPUT = ["message B 0", "message B 1"]

CLIENT_A_EXPECTED = (
    "1.1.1.1 3333 2.2.2.2 4444 4 message A 0",
    "1.1.1.1 3333 2.2.2.2 4444 4 message A 1",
)
CLIENT_B_EXPECTED = (
    "5.5.5.5 7777 6.6.6.6 8888 4 message B 0",
    "5.5.5.5 7777 6.6.6.6 8888 4 message B 1",
)


def test_pp_with_multiple_clients(config, port_allocator, syslog_ng):
    network_source = config.create_network_source(ip="localhost", port=port_allocator(), transport="proxied-tcp", flags="no-parse")
    file_destination = config.create_file_destination(file_name="output.log", template=TEMPLATE)
    config.create_logpath(statements=[network_source, file_destination])

    syslog_ng.start(config)

    # These 2 run simultaneously
    network_source.write_logs_with_proxy_header(PROXY_VERSION, CLIENT_A_PROXY_SRC_IP, CLIENT_A_PROXY_DST_IP, CLIENT_A_PROXY_SRC_PORT, CLIENT_A_PROXY_DST_PORT, CLIENT_A_INPUT, rate=1)
    network_source.write_logs_with_proxy_header(PROXY_VERSION, CLIENT_B_PROXY_SRC_IP, CLIENT_B_PROXY_DST_IP, CLIENT_B_PROXY_SRC_PORT, CLIENT_B_PROXY_DST_PORT, CLIENT_B_INPUT, rate=1)

    output_messages = file_destination.read_logs(counter=4)
    assert sorted(output_messages) == sorted(CLIENT_A_EXPECTED + CLIENT_B_EXPECTED)
