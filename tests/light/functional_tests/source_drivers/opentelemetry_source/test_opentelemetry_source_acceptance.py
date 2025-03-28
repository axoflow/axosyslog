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
import base64
import json
import typing

import pytest
from axosyslog_light.syslog_ng.syslog_ng import SyslogNg
from axosyslog_light.syslog_ng_config.statements.sources.opentelemetry_source import OTelLog
from axosyslog_light.syslog_ng_config.statements.sources.opentelemetry_source import OTelResource
from axosyslog_light.syslog_ng_config.statements.sources.opentelemetry_source import OTelResourceScopeLog
from axosyslog_light.syslog_ng_config.statements.sources.opentelemetry_source import OTelScope
from axosyslog_light.syslog_ng_config.syslog_ng_config import SyslogNgConfig


RESOURCE_1 = OTelResource(
    attributes={
        "string": "resource_1_string",
        "int": 1,
        "bool": True,
        "double": 1.1,
        "bytes": b"resource_1",
        "null": None,
        "list": ["resource_1_array"],
        "dict": {"key": "resource_1_value"},
    },
)
RESOURCE_1_OUTPUT = {
    "attributes": {
        "string": "resource_1_string",
        "int": 1,
        "bool": True,
        "double": 1.1,
        "null": None,
        "bytes": base64.b64encode(b"resource_1").decode("utf-8"),
        "list": ["resource_1_array"],
        "dict": {"key": "resource_1_value"},
    },
}

RESOURCE_2 = OTelResource(
    attributes={
        "string": "resource_2_string",
        "int": 2,
        "bool": False,
        "double": 2.2,
        "null": None,
        "bytes": b"resource_2",
        "list": ["resource_2_array"],
        "dict": {"key": "resource_2_value"},
    },
)
RESOURCE_2_OUTPUT = {
    "attributes": {
        "string": "resource_2_string",
        "int": 2,
        "bool": False,
        "double": 2.2,
        "null": None,
        "bytes": base64.b64encode(b"resource_2").decode("utf-8"),
        "list": ["resource_2_array"],
        "dict": {"key": "resource_2_value"},
    },
}

SCOPE_1 = OTelScope(
    name="scope_1",
    version="1.0",
    attributes={
        "string": "scope_1_string",
        "int": 1,
        "bool": True,
        "double": 1.1,
        "null": None,
        "bytes": b"scope_1",
        "list": ["scope_1_array"],
        "dict": {"key": "scope_1_value"},
    },
)
SCOPE_1_OUTPUT = {
    "name": "scope_1",
    "version": "1.0",
    "attributes": {
        "string": "scope_1_string",
        "int": 1,
        "bool": True,
        "double": 1.1,
        "null": None,
        "bytes": base64.b64encode(b"scope_1").decode("utf-8"),
        "list": ["scope_1_array"],
        "dict": {"key": "scope_1_value"},
    },
}

SCOPE_2 = OTelScope(
    name="scope_2",
    version="2.0",
    attributes={
        "string": "scope_2_string",
        "int": 2,
        "bool": False,
        "double": 2.2,
        "null": None,
        "bytes": b"scope_2",
        "list": ["scope_2_array"],
        "dict": {"key": "scope_2_value"},
    },
)
SCOPE_2_OUTPUT = {
    "name": "scope_2",
    "version": "2.0",
    "attributes": {
        "string": "scope_2_string",
        "int": 2,
        "bool": False,
        "double": 2.2,
        "null": None,
        "bytes": base64.b64encode(b"scope_2").decode("utf-8"),
        "list": ["scope_2_array"],
        "dict": {"key": "scope_2_value"},
    },
}

LOG_1 = OTelLog(
    time_unix_nano=1111111111000000000,
    observed_time_unix_nano=2222222222000000000,
    severity_number=3,
    severity_text="three",
    body="log_1",
    attributes={
        "string": "log_1_string",
        "int": 1,
        "bool": True,
        "double": 1.1,
        "null": None,
        "bytes": b"log_1",
        "list": ["log_1_array"],
        "dict": {"key": "log_1_value"},
    },
    flags=4,
    trace_id=b"trace_1",
    span_id=b"span_1",
)
LOG_1_OUTPUT = {
    "time_unix_nano": '1111111111.000000',
    "observed_time_unix_nano": '2222222222.000000',
    "severity_number": 3,
    "severity_text": "three",
    "body": "log_1",
    "attributes": {
        "string": "log_1_string",
        "int": 1,
        "bool": True,
        "double": 1.1,
        "null": None,
        "bytes": base64.b64encode(b"log_1").decode("utf-8"),
        "list": ["log_1_array"],
        "dict": {"key": "log_1_value"},
    },
    "flags": 4,
    "trace_id": base64.b64encode(b"trace_1").decode("utf-8"),
    "span_id": base64.b64encode(b"span_1").decode("utf-8"),
}

LOG_2 = OTelLog(
    time_unix_nano=5555555555000000000,
    observed_time_unix_nano=6666666666000000000,
    severity_number=7,
    severity_text="seven",
    body="log_2",
    attributes={
        "string": "log_2_string",
        "int": 2,
        "bool": False,
        "double": 2.2,
        "null": None,
        "bytes": b"log_2",
        "list": ["log_2_array"],
        "dict": {"key": "log_2_value"},
    },
    flags=8,
    trace_id=b"trace_2",
    span_id=b"span_2",
)
LOG_2_OUTPUT = {
    "time_unix_nano": '5555555555.000000',
    "observed_time_unix_nano": '6666666666.000000',
    "severity_number": 7,
    "severity_text": "seven",
    "body": "log_2",
    "attributes": {
        "string": "log_2_string",
        "int": 2,
        "bool": False,
        "double": 2.2,
        "null": None,
        "bytes": base64.b64encode(b"log_2").decode("utf-8"),
        "list": ["log_2_array"],
        "dict": {"key": "log_2_value"},
    },
    "flags": 8,
    "trace_id": base64.b64encode(b"trace_2").decode("utf-8"),
    "span_id": base64.b64encode(b"span_2").decode("utf-8"),
}


FILTERX = r"""
    resource = otel_resource(${.otel_raw.resource});
    scope = otel_scope(${.otel_raw.scope});
    log = otel_logrecord(${.otel_raw.log});

    $MSG = {
        "resource": resource,
        "scope": scope,
        "log": log,
    };
"""

TEMPLATE = '"$MSG\n"'


@pytest.mark.parametrize(
    "resource, resource_output, scope, scope_output, log, log_output",
    [
        (RESOURCE_1, RESOURCE_1_OUTPUT, SCOPE_1, SCOPE_1_OUTPUT, LOG_1, LOG_1_OUTPUT),
        (RESOURCE_1, RESOURCE_1_OUTPUT, None, dict(), LOG_1, LOG_1_OUTPUT),
        (None, dict(), SCOPE_1, SCOPE_1_OUTPUT, LOG_1, LOG_1_OUTPUT),
        (None, dict(), None, dict(), LOG_1, LOG_1_OUTPUT),
        (RESOURCE_1, RESOURCE_1_OUTPUT, SCOPE_1, SCOPE_1_OUTPUT, None, {"body": None}),
        (RESOURCE_1, RESOURCE_1_OUTPUT, None, dict(), None, {"body": None}),
        (None, dict(), SCOPE_1, SCOPE_1_OUTPUT, None, {"body": None}),
        (None, dict(), None, dict(), None, {"body": None}),
    ],
    ids=[
        "w_resource_w_scope_w_log",
        "w_resource_wo_scope_w_log",
        "wo_resource_w_scope_w_log",
        "wo_resource_wo_scope_w_log",
        "w_resource_w_scope_wo_log",
        "w_resource_wo_scope_wo_log",
        "wo_resource_w_scope_wo_log",
        "wo_resource_wo_scope_wo_log",
    ],
)
def test_opentelemetry_source_acceptance_single_log(
    syslog_ng: SyslogNg,
    config: SyslogNgConfig,
    port_allocator,
    resource: typing.Optional[OTelResource],
    resource_output: typing.Dict[str, typing.Any],
    scope: typing.Optional[OTelScope],
    scope_output: typing.Dict[str, typing.Any],
    log: typing.Optional[OTelLog],
    log_output: typing.Optional[OTelScope],
) -> None:
    opentelemetry_source = config.create_opentelemetry_source(port=port_allocator())
    filterx = config.create_filterx(FILTERX)
    file_destination = config.create_file_destination(file_name="output.log", template=TEMPLATE)
    config.create_logpath(statements=[opentelemetry_source, filterx, file_destination])

    syslog_ng.start(config)
    opentelemetry_source.write_log(resource=resource, scope=scope, log=log)
    assert json.loads(file_destination.read_log()) == {
        "resource": resource_output,
        "scope": scope_output,
        "log": log_output,
    }


def test_opentelemetry_source_acceptance_batch(
    syslog_ng: SyslogNg,
    config: SyslogNgConfig,
    port_allocator,
) -> None:
    opentelemetry_source = config.create_opentelemetry_source(port=port_allocator())
    filterx = config.create_filterx(FILTERX)
    file_destination = config.create_file_destination(file_name="output.log", template=TEMPLATE)
    config.create_logpath(statements=[opentelemetry_source, filterx, file_destination])

    logs: typing.List[OTelResourceScopeLog] = [
        OTelResourceScopeLog(RESOURCE_1, SCOPE_1, LOG_1),
        OTelResourceScopeLog(RESOURCE_2, SCOPE_1, LOG_1),
        OTelResourceScopeLog(RESOURCE_1, SCOPE_1, LOG_2),
        OTelResourceScopeLog(RESOURCE_2, SCOPE_1, LOG_2),
        OTelResourceScopeLog(RESOURCE_1, SCOPE_2, LOG_1),
        OTelResourceScopeLog(RESOURCE_2, SCOPE_2, LOG_1),
        OTelResourceScopeLog(RESOURCE_1, SCOPE_2, LOG_2),
        OTelResourceScopeLog(RESOURCE_2, SCOPE_2, LOG_2),
    ]
    expected_logs = [
        {"resource": RESOURCE_1_OUTPUT, "scope": SCOPE_1_OUTPUT, "log": LOG_1_OUTPUT},
        {"resource": RESOURCE_1_OUTPUT, "scope": SCOPE_1_OUTPUT, "log": LOG_2_OUTPUT},
        {"resource": RESOURCE_1_OUTPUT, "scope": SCOPE_2_OUTPUT, "log": LOG_1_OUTPUT},
        {"resource": RESOURCE_1_OUTPUT, "scope": SCOPE_2_OUTPUT, "log": LOG_2_OUTPUT},
        {"resource": RESOURCE_2_OUTPUT, "scope": SCOPE_1_OUTPUT, "log": LOG_1_OUTPUT},
        {"resource": RESOURCE_2_OUTPUT, "scope": SCOPE_1_OUTPUT, "log": LOG_2_OUTPUT},
        {"resource": RESOURCE_2_OUTPUT, "scope": SCOPE_2_OUTPUT, "log": LOG_1_OUTPUT},
        {"resource": RESOURCE_2_OUTPUT, "scope": SCOPE_2_OUTPUT, "log": LOG_2_OUTPUT},
    ]

    syslog_ng.start(config)
    opentelemetry_source.write_logs(logs)

    for expected_log in expected_logs:
        assert json.loads(file_destination.read_log()) == expected_log
