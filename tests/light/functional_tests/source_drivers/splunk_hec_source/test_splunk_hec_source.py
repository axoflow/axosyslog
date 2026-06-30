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
LOOPBACK = "127.0.0.1"


def test_splunk_hec_extracts_event_as_message(config, syslog_ng, port_allocator):
    config.add_include("scl.conf")

    hec_source = config.create_splunk_hec_source(port=port_allocator(), localip=LOOPBACK)
    file_destination = config.create_file_destination(file_name="output.log", template=r'"${MESSAGE}\n"')
    config.create_logpath(statements=[hec_source, file_destination])

    syslog_ng.start(config)

    hec_source.send_event("hello world")

    assert file_destination.read_log() == "hello world"


def test_splunk_hec_extracts_host_and_envelope(config, syslog_ng, port_allocator):
    config.add_include("scl.conf")

    hec_source = config.create_splunk_hec_source(port=port_allocator(), localip=LOOPBACK)
    file_destination = config.create_file_destination(
        file_name="output.log",
        template=r'"${HOST} ${MESSAGE} ${.splunk.sourcetype}\n"',
    )
    config.create_logpath(statements=[hec_source, file_destination])

    syslog_ng.start(config)

    hec_source.send_event("an event", host="myhost", sourcetype="nix:syslog")

    assert file_destination.read_log() == "myhost an event nix:syslog"


def test_splunk_hec_uses_event_time_as_timestamp(config, syslog_ng, port_allocator):
    config.add_include("scl.conf")

    hec_source = config.create_splunk_hec_source(port=port_allocator(), localip=LOOPBACK)
    file_destination = config.create_file_destination(file_name="output.log", template=r'"${S_UNIXTIME}\n"')
    config.create_logpath(statements=[hec_source, file_destination])

    syslog_ng.start(config)

    hec_source.send_event("timed event", time=1426279439)

    assert file_destination.read_log() == "1426279439"


def test_splunk_hec_batches_newline_delimited_events(config, syslog_ng, port_allocator):
    config.add_include("scl.conf")

    hec_source = config.create_splunk_hec_source(port=port_allocator(), localip=LOOPBACK)
    file_destination = config.create_file_destination(file_name="output.log", template=r'"${MESSAGE}\n"')
    config.create_logpath(statements=[hec_source, file_destination])

    syslog_ng.start(config)

    hec_source.send_events(["first", "second", "third"])

    assert file_destination.read_logs(3) == ["first", "second", "third"]


def test_splunk_hec_accepts_bare_collector_endpoint(config, syslog_ng, port_allocator):
    config.add_include("scl.conf")

    hec_source = config.create_splunk_hec_source(port=port_allocator(), localip=LOOPBACK)
    file_destination = config.create_file_destination(file_name="output.log", template=r'"${MESSAGE}\n"')
    config.create_logpath(statements=[hec_source, file_destination])

    syslog_ng.start(config)

    response = hec_source.post('{"event": "bare endpoint"}', path="/services/collector")
    assert response.status_code == 200

    assert file_destination.read_log() == "bare endpoint"
