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
import pytest
from axosyslog_light.syslog_ng.syslog_ng import SyslogNg
from axosyslog_light.syslog_ng_config.syslog_ng_config import SyslogNgConfig

GENERATOR_MESSAGE = "plain-body"

ASSEMBLE_PAYLOAD = """
    ${.axo.type} = "log";
    ${.axo.resource} = otel_resource({"attributes": {"service.name": "axo-svc"}});
    ${.axo.scope} = otel_scope({"name": "axo-scope", "version": "v9"});
    ${.axo.log} = otel_logrecord({"body": "axo-body", "severity_text": "ERR"});
"""

ASSEMBLE_RAW_PAYLOAD = """
    ${.otel_raw.type} = "log";
    ${.otel_raw.log} = otel_logrecord({"body": "raw-body", "severity_text": "RAW"});
"""

ASSEMBLE_OTEL_FIELDS = """
    ${.otel.type} = "log";
    ${.otel.log.body} = "fallback-body";
    ${.otel.log.severity_text} = "FALLBACK";
"""


DECODE_RECEIVED = """
    log_record = otel_logrecord(${.otel_raw.log});
    resource = otel_resource(${.otel_raw.resource}) ?? otel_resource();
    scope = otel_scope(${.otel_raw.scope}) ?? otel_scope();
    $MSG = "type=" + ${.otel_raw.type}
           + " body=" + string(log_record.body)
           + " severity=" + log_record.severity_text
           + " service=" + (string(resource.attributes["service.name"]) ?? "")
           + " scope=" + scope.name;
"""


@pytest.mark.parametrize(
    "assemble_payload, prefix, expected",
    [
        pytest.param(
            ASSEMBLE_PAYLOAD, ".axo",
            "type=log body=axo-body severity=ERR service=axo-svc scope=axo-scope",
            id="prefix-axo-payload-axo-expect-axo-payload",
        ),
        # prefix() replaces .otel_raw instead of adding to it: a message that only
        # carries .otel_raw.* is sent as a plain syslog-ng fallback record built from $MSG
        pytest.param(
            ASSEMBLE_RAW_PAYLOAD, ".axo",
            f"type=log body={GENERATOR_MESSAGE} severity= service= scope=",
            id="prefix-axo-payload-otel_raw-expect-fallback-payload",
        ),
        # the per-field .otel.* namespace is still read when <prefix>.log is absent
        pytest.param(
            ASSEMBLE_OTEL_FIELDS, ".axo",
            "type=log body=fallback-body severity=FALLBACK service= scope=",
            id="prefix-axo-payload-otel-expect-otel-payload",
        ),
        # prefix("") is accepted and selects the default .otel_raw namespace
        pytest.param(
            ASSEMBLE_RAW_PAYLOAD, "",
            "type=log body=raw-body severity=RAW service= scope=",
            id="prefix-empty-payload-otel_raw-expect-otel_raw-payload",
        ),
    ],
)
def test_opentelemetry_destination_prefix(
    syslog_ng: SyslogNg,
    config: SyslogNgConfig,
    port_allocator,
    assemble_payload: str,
    prefix: str,
    expected: str,
) -> None:
    receiver_port = port_allocator()

    sender_source = config.create_example_msg_generator_source(num=1, template=config.stringify(GENERATOR_MESSAGE))
    destination = config.create_opentelemetry_destination(port=receiver_port, prefix=config.stringify(prefix))
    config.create_logpath(statements=[sender_source, config.create_filterx(assemble_payload), destination])

    receiver_source = config.create_opentelemetry_source(port=receiver_port)
    file_destination = config.create_file_destination(file_name="output.log", template='"${MSG}\\n"')
    config.create_logpath(statements=[receiver_source, config.create_filterx(DECODE_RECEIVED), file_destination])

    syslog_ng.start(config)

    assert file_destination.read_log() == expected
