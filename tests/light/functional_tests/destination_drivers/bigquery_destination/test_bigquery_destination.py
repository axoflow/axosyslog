#!/usr/bin/env python
#############################################################################
# Copyright (c) 2026 Axoflow
# Copyright (c) 2026 Attila Szakacs <attila.szakacs@axoflow.com>
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
import uuid

from axosyslog_light.common.blocking import wait_until_true
from axosyslog_light.syslog_ng_config.__init__ import stringify

BIGQUERY_OPTIONS = {
    "schema": '"message" STRING => $MSG',
}


def test_bigquery_destination_single_message(config, syslog_ng, port_allocator):
    custom_msg = f"test message {uuid.uuid4()}"
    generator_source = config.create_example_msg_generator_source(num=1, template=stringify(custom_msg))

    bigquery_destination = config.create_bigquery_destination(port=port_allocator(), **BIGQUERY_OPTIONS)
    config.create_logpath(statements=[generator_source, bigquery_destination])

    bigquery_destination.start_listener()
    syslog_ng.start(config)

    assert wait_until_true(lambda: bigquery_destination.get_stats().get("written", 0) == 1)
    assert bigquery_destination.get_stats()["dropped"] == 0
    assert bigquery_destination.get_row_count() == 1
    assert bigquery_destination.read_logs() == [{"message": custom_msg}]
    assert bigquery_destination.get_stream_count() == 1
    assert bigquery_destination.get_create_count() == 1
