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
import typing
from dataclasses import dataclass
from dataclasses import field

from grpc import insecure_channel
from opentelemetry.proto.collector.logs.v1.logs_service_pb2 import ExportLogsServiceRequest
from opentelemetry.proto.collector.logs.v1.logs_service_pb2_grpc import LogsServiceStub
from opentelemetry.proto.common.v1.common_pb2 import AnyValue
from opentelemetry.proto.common.v1.common_pb2 import ArrayValue
from opentelemetry.proto.common.v1.common_pb2 import InstrumentationScope
from opentelemetry.proto.common.v1.common_pb2 import KeyValue
from opentelemetry.proto.common.v1.common_pb2 import KeyValueList
from opentelemetry.proto.logs.v1.logs_pb2 import LogRecord
from opentelemetry.proto.logs.v1.logs_pb2 import ResourceLogs
from opentelemetry.proto.logs.v1.logs_pb2 import ScopeLogs
from opentelemetry.proto.logs.v1.logs_pb2 import SEVERITY_NUMBER_DEBUG
from opentelemetry.proto.logs.v1.logs_pb2 import SEVERITY_NUMBER_DEBUG2
from opentelemetry.proto.logs.v1.logs_pb2 import SEVERITY_NUMBER_DEBUG3
from opentelemetry.proto.logs.v1.logs_pb2 import SEVERITY_NUMBER_DEBUG4
from opentelemetry.proto.logs.v1.logs_pb2 import SEVERITY_NUMBER_ERROR
from opentelemetry.proto.logs.v1.logs_pb2 import SEVERITY_NUMBER_ERROR2
from opentelemetry.proto.logs.v1.logs_pb2 import SEVERITY_NUMBER_ERROR3
from opentelemetry.proto.logs.v1.logs_pb2 import SEVERITY_NUMBER_ERROR4
from opentelemetry.proto.logs.v1.logs_pb2 import SEVERITY_NUMBER_FATAL
from opentelemetry.proto.logs.v1.logs_pb2 import SEVERITY_NUMBER_FATAL2
from opentelemetry.proto.logs.v1.logs_pb2 import SEVERITY_NUMBER_FATAL3
from opentelemetry.proto.logs.v1.logs_pb2 import SEVERITY_NUMBER_FATAL4
from opentelemetry.proto.logs.v1.logs_pb2 import SEVERITY_NUMBER_INFO
from opentelemetry.proto.logs.v1.logs_pb2 import SEVERITY_NUMBER_INFO2
from opentelemetry.proto.logs.v1.logs_pb2 import SEVERITY_NUMBER_INFO3
from opentelemetry.proto.logs.v1.logs_pb2 import SEVERITY_NUMBER_INFO4
from opentelemetry.proto.logs.v1.logs_pb2 import SEVERITY_NUMBER_TRACE
from opentelemetry.proto.logs.v1.logs_pb2 import SEVERITY_NUMBER_TRACE2
from opentelemetry.proto.logs.v1.logs_pb2 import SEVERITY_NUMBER_TRACE3
from opentelemetry.proto.logs.v1.logs_pb2 import SEVERITY_NUMBER_TRACE4
from opentelemetry.proto.logs.v1.logs_pb2 import SEVERITY_NUMBER_UNSPECIFIED
from opentelemetry.proto.logs.v1.logs_pb2 import SEVERITY_NUMBER_WARN
from opentelemetry.proto.logs.v1.logs_pb2 import SEVERITY_NUMBER_WARN2
from opentelemetry.proto.logs.v1.logs_pb2 import SEVERITY_NUMBER_WARN3
from opentelemetry.proto.logs.v1.logs_pb2 import SEVERITY_NUMBER_WARN4
from opentelemetry.proto.resource.v1.resource_pb2 import Resource


class PyToOTelConverter:
    @staticmethod
    def convert_value_to_any_value(value: typing.Any) -> AnyValue:
        if isinstance(value, str):
            return AnyValue(string_value=value)
        if isinstance(value, bool):
            return AnyValue(bool_value=value)
        if isinstance(value, int):
            return AnyValue(int_value=value)
        if isinstance(value, float):
            return AnyValue(double_value=value)
        if isinstance(value, bytes):
            return AnyValue(bytes_value=value)
        if isinstance(value, dict):
            return AnyValue(kvlist_value=PyToOTelConverter.convert_dict_to_key_value_list(value))
        if isinstance(value, list):
            return AnyValue(array_value=PyToOTelConverter.convert_list_to_values(value))
        if isinstance(value, type(None)):
            return AnyValue()
        raise ValueError("Unsupported value type: {}".format(type(value)))

    @staticmethod
    def convert_dict_to_key_value_list(data: typing.Dict[str, typing.Any]) -> KeyValueList:
        key_value_list = KeyValueList()

        for key, value in data.items():
            if not isinstance(key, str):
                raise ValueError("Key must be a string: {}".format(key))

            key_value = KeyValue()
            key_value.key = key
            key_value.value.CopyFrom(PyToOTelConverter.convert_value_to_any_value(value))
            key_value_list.values.append(key_value)

        return key_value_list

    @staticmethod
    def convert_list_to_values(data: typing.List[typing.Any]) -> ArrayValue:
        array_value = ArrayValue()

        for value in data:
            array_value.values.append(PyToOTelConverter.convert_value_to_any_value(value))

        return array_value


@dataclass
class OTelResource:
    attributes: typing.Dict[str, typing.Any] = field(default_factory=dict)

    def to_otel(self) -> Resource:
        resource = Resource()
        resource.attributes.extend(PyToOTelConverter.convert_dict_to_key_value_list(self.attributes).values)
        return resource


@dataclass
class OTelScope:
    name: str = ""
    version: str = ""
    attributes: typing.Dict[str, typing.Any] = field(default_factory=dict)

    def to_otel(self) -> InstrumentationScope:
        scope = InstrumentationScope()
        scope.name = self.name
        scope.version = self.version
        scope.attributes.extend(PyToOTelConverter.convert_dict_to_key_value_list(self.attributes).values)
        return scope


@dataclass
class OTelLog:
    time_unix_nano: int = 0
    observed_time_unix_nano: int = 0
    severity_number: int = 0
    severity_text: str = ""
    body: typing.Any = None
    attributes: typing.Dict[str, typing.Any] = field(default_factory=dict)
    flags: int = 0
    trace_id: bytes = b""
    span_id: bytes = b""

    def to_otel(self) -> LogRecord:
        SEVERITY_NUMBERS = [
            SEVERITY_NUMBER_UNSPECIFIED,
            SEVERITY_NUMBER_TRACE,
            SEVERITY_NUMBER_TRACE2,
            SEVERITY_NUMBER_TRACE3,
            SEVERITY_NUMBER_TRACE4,
            SEVERITY_NUMBER_DEBUG,
            SEVERITY_NUMBER_DEBUG2,
            SEVERITY_NUMBER_DEBUG3,
            SEVERITY_NUMBER_DEBUG4,
            SEVERITY_NUMBER_INFO,
            SEVERITY_NUMBER_INFO2,
            SEVERITY_NUMBER_INFO3,
            SEVERITY_NUMBER_INFO4,
            SEVERITY_NUMBER_WARN,
            SEVERITY_NUMBER_WARN2,
            SEVERITY_NUMBER_WARN3,
            SEVERITY_NUMBER_WARN4,
            SEVERITY_NUMBER_ERROR,
            SEVERITY_NUMBER_ERROR2,
            SEVERITY_NUMBER_ERROR3,
            SEVERITY_NUMBER_ERROR4,
            SEVERITY_NUMBER_FATAL,
            SEVERITY_NUMBER_FATAL2,
            SEVERITY_NUMBER_FATAL3,
            SEVERITY_NUMBER_FATAL4,
        ]

        log = LogRecord()
        log.time_unix_nano = self.time_unix_nano
        log.observed_time_unix_nano = self.observed_time_unix_nano
        log.severity_number = SEVERITY_NUMBERS[self.severity_number]
        log.severity_text = self.severity_text
        log.body.CopyFrom(PyToOTelConverter.convert_value_to_any_value(self.body))
        log.attributes.extend(PyToOTelConverter.convert_dict_to_key_value_list(self.attributes).values)
        log.flags = self.flags
        log.trace_id = self.trace_id
        log.span_id = self.span_id
        return log


class OTelResourceScopeLog:
    def __init__(
        self,
        resource: typing.Optional[OTelResource] = None,
        scope: typing.Optional[OTelScope] = None,
        log: typing.Optional[OTelLog] = None,
    ) -> None:
        self.resource = resource if resource is not None else OTelResource()
        self.scope = scope if scope is not None else OTelScope()
        self.log = log if log is not None else OTelLog()


class OpenTelemetryIO():
    def __init__(self, port: int) -> None:
        self.__port = port

    def __create_request(self, resource_scope_logs: typing.List[OTelResourceScopeLog]) -> ExportLogsServiceRequest:
        request = ExportLogsServiceRequest()
        for resource_scope_log in resource_scope_logs:
            resource_logs = ResourceLogs()
            resource_logs.resource.CopyFrom(resource_scope_log.resource.to_otel())

            new_resource_logs = True
            for stored_resource_logs in request.resource_logs:
                if stored_resource_logs.resource == resource_logs.resource:
                    resource_logs = stored_resource_logs
                    new_resource_logs = False
                    break
            if new_resource_logs:
                request.resource_logs.append(resource_logs)
                resource_logs = request.resource_logs[-1]

            scope_logs = ScopeLogs()
            scope_logs.scope.CopyFrom(resource_scope_log.scope.to_otel())

            new_scope_logs = True
            for stored_scope_logs in resource_logs.scope_logs:
                if stored_scope_logs.scope == scope_logs.scope:
                    scope_logs = stored_scope_logs
                    new_scope_logs = False
                    break
            if new_scope_logs:
                resource_logs.scope_logs.append(scope_logs)
                scope_logs = resource_logs.scope_logs[-1]

            log_record = resource_scope_log.log.to_otel()
            scope_logs.log_records.append(log_record)

        return request

    def send_logs(self, resource_scope_logs: typing.List[OTelResourceScopeLog]) -> None:
        request = self.__create_request(resource_scope_logs)
        with insecure_channel(f"localhost:{self.__port}") as channel:
            stub = LogsServiceStub(channel)
            stub.Export(request)
