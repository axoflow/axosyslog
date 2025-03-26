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

PROXY_VERSION = 1
PROXY_SRC_IP = "192.168.1.1"
PROXY_DST_IP = "192.168.1.2"
PROXY_SRC_PORT = 20000
PROXY_DST_PORT = 20001
RFC3164_EXAMPLE = ["<34>Oct 11 22:14:15 mymachine su: 'su root' failed for lonvick on /dev/pts/8"]
RFC3164_EXAMPLE_WITHOUT_PRI = "Oct 11 22:14:15 mymachine su: 'su root' failed for lonvick on /dev/pts/8"


def test_pp_with_syslog_proto(config, port_allocator, syslog_ng):
    network_source = config.create_network_source(ip="localhost", port=port_allocator(), transport="proxied-tcp")
    file_destination = config.create_file_destination(file_name="output.log")
    config.create_logpath(statements=[network_source, file_destination])
    config.update_global_options(keep_hostname="yes")

    syslog_ng.start(config)

    network_source.write_logs_with_proxy_header(PROXY_VERSION, PROXY_SRC_IP, PROXY_DST_IP, PROXY_SRC_PORT, PROXY_DST_PORT, RFC3164_EXAMPLE)

    assert file_destination.read_log() == RFC3164_EXAMPLE_WITHOUT_PRI
