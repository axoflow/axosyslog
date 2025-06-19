#!/usr/bin/env python
#############################################################################
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
from axosyslog_light.common.file import copy_shared_file


def test_kept_alive_tls_connection_doing_handshake_after_reload(config, syslog_ng, port_allocator, testcase_parameters):
    ca = copy_shared_file(testcase_parameters, "valid-ca.crt")

    network_source = config.create_network_source(port=port_allocator())
    tls_network_destination = config.create_network_destination(
        ip="localhost", port=port_allocator(), transport="tls",
        keep_alive="yes",
        tls={
            "ca-file": ca,
            "peer-verify": "yes",
        },
    )

    config.create_logpath(statements=[network_source, tls_network_destination])

    tls_network_destination.start_listener()
    syslog_ng.start(config)

    syslog_ng.reload(config)
    network_source.write_log("test msg")

    assert tls_network_destination.read_until_logs(["test msg"])
