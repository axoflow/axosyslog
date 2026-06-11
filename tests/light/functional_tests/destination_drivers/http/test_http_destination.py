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


OPENOBSERVE_PARTIAL_FAILURE = '{"code":200,"status":[{"name":"stream","successful":0,"failed":1,"error":"Cant parse timestamp"}]}'
OPENOBSERVE_SUCCESS = '{"code":200,"status":[{"name":"stream","successful":1,"failed":0}]}'


def test_http_destination_accepts_messages(config, syslog_ng, port_allocator):
    port = port_allocator()
    num_messages = 5

    generator_source = config.create_example_msg_generator_source(
        num=num_messages,
        template=config.stringify("test message"),
    )
    http_destination = config.create_http_destination(
        port=port,
        body=config.stringify("${MSG} ${UNIQID}"),
    )
    config.create_logpath(statements=[generator_source, http_destination])

    syslog_ng.start(config)

    messages = http_destination.read_logs(num_messages)
    assert len(messages) == num_messages
    assert all("test message" in msg for msg in messages)


def test_http_destination_openobserve_retries_on_partial_failure(config, syslog_ng, port_allocator):
    port = port_allocator()

    generator_source = config.create_example_msg_generator_source(
        num=1,
        template=config.stringify("test message"),
    )
    http_destination = config.create_http_destination(
        port=port,
        body=config.stringify("${MSG}"),
        time_reopen=1,
        response_adapter=config.stringify("openobserve"),
        responses=[
            (200, OPENOBSERVE_PARTIAL_FAILURE),
            (200, OPENOBSERVE_SUCCESS),
        ],
    )
    config.create_logpath(statements=[generator_source, http_destination])

    syslog_ng.start(config)

    # Expect 2 POST requests: initial attempt (failure) + retry (success)
    requests = http_destination.read_logs(2)
    assert len(requests) == 2
    assert all("test message" in r for r in requests)
