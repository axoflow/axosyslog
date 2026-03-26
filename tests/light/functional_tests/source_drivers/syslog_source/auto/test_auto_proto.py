#!/usr/bin/env python
#############################################################################
# Copyright (c) 2024 Balazs Scheidler <balazs.scheidler@axoflow.com>
# Copyright (c) 2024 Axoflow
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
import pytest
from axosyslog_light.driver_io.network.network_io import NetworkIO


_test_cases = [
    (
        ["<2>Oct 11 22:14:15 myhostname sshd[1234]: message 0"] * 10,
        ["Oct 11 22:14:15 myhostname sshd[1234]: message 0"] * 10,
    ),
]
_test_case_ids = [
    "legacy_syslog",
]


def _test_auto_detect(config, syslog_ng, port_allocator, framed, input_message, expected_message):
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

    syslog_source.write_logs(input_message, transport=NetworkIO.Transport.TCP, framed=framed)

    assert file_destination.read_all() == expected_message


@pytest.mark.parametrize("input_message, expected_message", _test_cases, ids=_test_case_ids)
def test_auto_framing(config, syslog_ng, port_allocator, input_message, expected_message):
    _test_auto_detect(config, syslog_ng, port_allocator, True, input_message, expected_message)


@pytest.mark.parametrize("input_message, expected_message", _test_cases, ids=_test_case_ids)
def test_auto_no_framing(config, syslog_ng, port_allocator, input_message, expected_message):
    _test_auto_detect(config, syslog_ng, port_allocator, False, input_message, expected_message)
