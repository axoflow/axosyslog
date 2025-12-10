#!/usr/bin/env python
#############################################################################
# Copyright (c) 2020 One Identity
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

TEMPLATE = r'"${MESSAGE}\n"'


def test_nul_nl_termination(config, syslog_ng, port_allocator):
    INPUT_MESSAGE = "prog message nul terminated\x00\n"
    EXPECTED_MESSAGE0 = "message nul terminated"

    network_source = config.create_network_source(ip="localhost", port=port_allocator(), transport='"nul-terminated"', flags="no-multi-line")
    file_destination = config.create_file_destination(file_name="output.log", template=TEMPLATE)
    config.create_logpath(statements=[network_source, file_destination])

    syslog_ng.start(config)

    network_source.write_log(INPUT_MESSAGE * 3)

    assert file_destination.read_log() == EXPECTED_MESSAGE0
    assert file_destination.read_log() == EXPECTED_MESSAGE0
    assert file_destination.read_log() == EXPECTED_MESSAGE0


def test_multiline_messages_with_nul_termination(config, syslog_ng, port_allocator):
    INPUT_MESSAGE = "prog message\nnul\nterminated\x00\n"
    EXPECTED_MESSAGE0 = "message nul terminated"

    network_source = config.create_network_source(ip="localhost", port=port_allocator(), transport='"nul-terminated"', flags="no-multi-line")
    file_destination = config.create_file_destination(file_name="output.log", template=TEMPLATE)
    config.create_logpath(statements=[network_source, file_destination])

    syslog_ng.start(config)

    network_source.write_log(INPUT_MESSAGE)
    # newlines are replaced by spaces due to flags(no-multi-line)
    assert file_destination.read_log() == EXPECTED_MESSAGE0
