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
INPUT_JSON_LOG = {"message": "test message"}
EXPECTED_LOG = "test message"


def test_webhook_source_acceptance(config, syslog_ng, port_allocator):
    config.add_include("scl.conf")

    webhook_source = config.create_webhook_source(port=port_allocator())
    file_destination = config.create_file_destination(file_name="output.log", template=r'"${MESSAGE}\n"')
    config.create_logpath(statements=[webhook_source, file_destination])

    syslog_ng.start(config)

    webhook_source.write_log(INPUT_LOG)
    assert file_destination.read_log() == EXPECTED_LOG


def test_webhook_json_source_acceptance(config, syslog_ng, port_allocator):
    config.add_include("scl.conf")

    webhook_json_source = config.create_webhook_json_source(port=port_allocator(), prefix=".webhook-light.")
    file_destination = config.create_file_destination(file_name="output.log", template=r'"${.webhook-light.message}\n"')
    config.create_logpath(statements=[webhook_json_source, file_destination])

    syslog_ng.start(config)

    webhook_json_source.write_log(INPUT_JSON_LOG)
    assert file_destination.read_log() == EXPECTED_LOG
