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
import pytest
from axosyslog_light.helpers.snmptrapd.conftest import *  # noqa:F403, F401


@pytest.mark.snmp
def test_snmp_dest_v2c_cisco_ios_trap(config, syslog_ng, snmptrapd, snmp_test_params):
    generator_source = config.create_example_msg_generator_source(num=1)
    snmp_destination = config.create_snmp_destination(
        host=snmp_test_params.get_ip_address(),
        port=snmptrapd.get_port(),
        snmp_obj=snmp_test_params.get_cisco_snmp_obj(),
        trap_obj=snmp_test_params.get_basic_trap_obj(),
        version="v2c",
    )
    config.create_logpath(statements=[generator_source, snmp_destination])

    syslog_ng.start(config)

    expected_traps = snmp_test_params.get_expected_cisco_trap()
    assert expected_traps == snmptrapd.get_traps(len(expected_traps))
