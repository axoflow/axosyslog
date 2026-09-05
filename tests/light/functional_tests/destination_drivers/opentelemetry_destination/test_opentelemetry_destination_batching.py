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
from axosyslog_light.syslog_ng_config.statements.sources.opentelemetry_source import OTelLog
from axosyslog_light.syslog_ng_config.syslog_ng_config import SyslogNgConfig
from grpc import insecure_channel
from opentelemetry.proto.collector.metrics.v1.metrics_service_pb2 import ExportMetricsServiceRequest
from opentelemetry.proto.collector.metrics.v1.metrics_service_pb2_grpc import MetricsServiceStub
from opentelemetry.proto.metrics.v1.metrics_pb2 import Metric


def _send_metric(port: int, metric: Metric) -> None:
    request = ExportMetricsServiceRequest()
    request.resource_metrics.add().scope_metrics.add().metrics.append(metric)
    with insecure_channel(f"127.0.0.1:{port}") as channel:
        MetricsServiceStub(channel).Export(request)


def test_opentelemetry_destination_mixed_signal_batch(
    syslog_ng: SyslogNg,
    config: SyslogNgConfig,
    port_allocator,
) -> None:
    sender_port = port_allocator()
    receiver_port = port_allocator()

    sender_source = config.create_opentelemetry_source(port=sender_port)
    # without batch-timeout() the worker flushes whenever its queue runs empty,
    # so the two signals could go out in two batches
    opentelemetry_destination = config.create_opentelemetry_destination(
        port=receiver_port,
        batch_lines=2,
        batch_timeout=10000,
    )
    config.create_logpath(statements=[sender_source, opentelemetry_destination])

    receiver_source = config.create_opentelemetry_source(port=receiver_port)
    file_destination = config.create_file_destination(file_name="output.log", template='"${.otel_raw.type}\\n"')
    config.create_logpath(statements=[receiver_source, file_destination])

    syslog_ng.start(config)
    sender_source.write_log(log=OTelLog(body="log"))
    _send_metric(sender_port, Metric(name="metric"))

    assert sorted(file_destination.read_logs(2)) == ["log", "metric"]
    assert syslog_ng.is_process_running()
