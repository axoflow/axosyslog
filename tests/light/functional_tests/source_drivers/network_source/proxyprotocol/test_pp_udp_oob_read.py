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
import socket
import time

from axosyslog_light.syslog_ng_config.renderer import render_statement


PROXY_V2_SIGNATURE = bytes([0x0D, 0x0A, 0x0D, 0x0A, 0x00, 0x0D, 0x0A, 0x51, 0x55, 0x49, 0x54, 0x0A])


def test_proxy_protocol_udp_declared_length_overrun(config, port_allocator, syslog_ng):
    port = port_allocator()
    dest = config.create_file_destination(file_name="dest.log", template="'$MSG\n'")
    raw = f"""
@version: {config.get_version()}

source s_net {{
    network(transport("proxied-udp") ip("127.0.0.1") port({port}));
}};

destination dest {{
    {render_statement(dest)};
}};

log {{
    source(s_net);
    destination(dest);
}};
"""
    config.set_raw_config(raw)
    syslog_ng.start(config)

    header = PROXY_V2_SIGNATURE + bytes([0x21, 0x12]) + (0xFFFF).to_bytes(2, "big")
    datagram = header + bytes(12)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        sock.sendto(datagram, ("127.0.0.1", port))
    finally:
        sock.close()

    time.sleep(2)

    assert syslog_ng.is_process_running()
    syslog_ng.stop()
