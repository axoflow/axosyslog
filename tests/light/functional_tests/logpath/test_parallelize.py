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
from axosyslog_light.syslog_ng_config.statements.logpath.log_context_scl_block import LogContextSclBlock


def test_parallelize_source_without_peer_address(config, syslog_ng):
    # file() carries neither a peer nor a local address, and posts from an
    # I/O worker thread, so its messages reach the parallelize partitions
    file_source = config.create_file_source(file_name="input.log", flags="no-parse")
    parallelize = LogContextSclBlock("parallelize", workers=2)
    file_destination = config.create_file_destination(file_name="output.log", template=config.stringify("$MSG\n"))
    config.create_logpath(statements=[file_source, parallelize, file_destination])

    syslog_ng.start(config)
    messages = [f"message {i}" for i in range(100)]
    file_source.write_logs(messages)

    assert sorted(file_destination.read_logs(100)) == sorted(messages)
