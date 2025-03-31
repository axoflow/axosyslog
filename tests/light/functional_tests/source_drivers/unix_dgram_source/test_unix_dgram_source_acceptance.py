#!/usr/bin/env python
#############################################################################
# Copyright (c) 2025 Axoflow
# Copyright (c) 2025 Attila Szakacs <attila.szakacs@axoflow.com>
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
INPUT_LOG = "test message"


def test_unix_dgram_source_acceptance(config, syslog_ng):
    config.update_global_options(stats_level=1)
    unix_dgram_source = config.create_unix_dgram_source(path="input.sock", flags="no-parse")
    file_destination = config.create_file_destination(file_name="output.log", template='"$MSG\n"')
    config.create_logpath(statements=[unix_dgram_source, file_destination])

    syslog_ng.start(config)

    unix_dgram_source.write_log(INPUT_LOG)
    assert file_destination.read_log() == INPUT_LOG
    assert unix_dgram_source.get_stats()["processed"] == 1

    number_of_logs = 10

    unix_dgram_source.write_logs([INPUT_LOG] * number_of_logs)
    assert file_destination.read_logs(number_of_logs) == [INPUT_LOG] * number_of_logs
    assert unix_dgram_source.get_stats()["processed"] == 1 + number_of_logs
