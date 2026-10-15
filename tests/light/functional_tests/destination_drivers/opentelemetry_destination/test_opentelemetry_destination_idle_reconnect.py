#!/usr/bin/env python
#############################################################################
# Copyright (c) 2026 Axoflow
# Copyright (c) 2026 Attila Szakacs-Bertok <attila.szakacs@axoflow.com>
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
from axosyslog_light.syslog_ng.syslog_ng import SyslogNg
from axosyslog_light.syslog_ng_config.syslog_ng_config import SyslogNgConfig


def test_opentelemetry_destination_idle_reconnect(
    syslog_ng: SyslogNg,
    config: SyslogNgConfig,
    port_allocator,
) -> None:
    receiver_port = port_allocator()

    generator_source = config.create_example_msg_generator_source(num=2, freq=2)
    opentelemetry_destination = config.create_opentelemetry_destination(
        port=receiver_port,
        idle_reconnect_timeout=1,
    )
    config.create_logpath(statements=[generator_source, opentelemetry_destination])

    receiver_source = config.create_opentelemetry_source(port=receiver_port)
    file_destination = config.create_file_destination(file_name="output.log", template='"${.otel_raw.type}\\n"')
    config.create_logpath(statements=[receiver_source, file_destination])

    syslog_ng.start(config)

    assert file_destination.read_logs(2) == ["log", "log"]
    assert syslog_ng.is_message_in_console_log("Reopening idle gRPC channel")
