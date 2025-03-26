#!/usr/bin/env python
#############################################################################
# Copyright (c) 2024 Balazs Scheidler <balazs.scheidler@axoflow.com>
# Copyright (c) 2024 Axoflow
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


def _test_auto_detect(config, syslog_ng, port_allocator, framed):
    NUMBER_OF_MESSAGES = 10
    INPUT_MESSAGES = ["<2>Oct 11 22:14:15 myhostname sshd[1234]: message 0"] * NUMBER_OF_MESSAGES
    EXPECTED_MESSAGES = ["Oct 11 22:14:15 myhostname sshd[1234]: message 0"] * NUMBER_OF_MESSAGES

    OUTPUT_FILE = "output.log"

    syslog_source = config.create_syslog_source(
        ip="localhost",
        port=port_allocator(),
        keep_hostname="yes",
        transport="auto",
    )
    file_destination = config.create_file_destination(file_name=OUTPUT_FILE)
    config.create_logpath(statements=[syslog_source, file_destination])

    syslog_ng.start(config)

    syslog_source.write_logs(INPUT_MESSAGES, transport=NetworkIO.Transport.TCP, framed=framed)

    assert file_destination.read_logs(NUMBER_OF_MESSAGES) == EXPECTED_MESSAGES


def test_auto_framing(config, syslog_ng, port_allocator):
    _test_auto_detect(config, syslog_ng, port_allocator, True)


def test_auto_no_framing(config, syslog_ng, port_allocator):
    _test_auto_detect(config, syslog_ng, port_allocator, False)
