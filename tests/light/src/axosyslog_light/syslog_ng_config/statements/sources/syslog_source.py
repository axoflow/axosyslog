#!/usr/bin/env python
#############################################################################
# Copyright (c) 2015-2020 Balabit
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
from axosyslog_light.driver_io.network.network_io import NetworkIO
from axosyslog_light.syslog_ng_config.statements.sources.source_driver import SourceDriver


def map_transport(transport):
    mapping = {
        "auto": NetworkIO.Transport.AUTO,
        "tcp": NetworkIO.Transport.TCP,
        "udp": NetworkIO.Transport.UDP,
        "tls": NetworkIO.Transport.TLS,
        "proxied-tcp": NetworkIO.Transport.PROXIED_TCP,
        "proxied-tls": NetworkIO.Transport.PROXIED_TLS,
        "proxied-tls-passthrough": NetworkIO.Transport.PROXIED_TLS_PASSTHROUGH,
    }
    transport = transport.replace("_", "-").replace("'", "").replace('"', "").lower()

    return mapping[transport]


def create_io(options):
    ip = options["ip"] if "ip" in options else "localhost"
    transport = options["transport"] if "transport" in options else "tcp"

    return NetworkIO(ip, options["port"], map_transport(transport), True)


class SyslogSource(SourceDriver):
    def __init__(self, **options):
        self.io = create_io(options)

        self.driver_name = "syslog"
        super(SyslogSource, self).__init__(options=options)

    def write_log(self, message, rate=None, transport=None, framed=None):
        self.io.write_messages([message], rate=rate, transport=transport, framed=framed)

    def write_logs(self, messages, rate=None, transport=None, framed=None):
        self.io.write_messages(messages, rate=rate, transport=transport, framed=framed)

    def write_logs_with_proxy_header(self, proxy_version, src_ip, dst_ip, src_port, dst_port, messages, rate=None, transport=None, framed=None):
        self.io.write_messages_with_proxy_header(proxy_version, src_ip, dst_ip, src_port, dst_port, messages, rate=rate, transport=transport, framed=framed)
