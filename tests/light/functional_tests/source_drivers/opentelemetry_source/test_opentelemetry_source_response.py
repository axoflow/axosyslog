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
import time
from concurrent.futures import ThreadPoolExecutor

import grpc
import pytest
from axosyslog_light.common.blocking import DEFAULT_TIMEOUT
from axosyslog_light.common.blocking import wait_until_true
from axosyslog_light.common.blocking import wait_until_true_custom
from axosyslog_light.syslog_ng_config.statements.sources.opentelemetry_source import OTelLog
from axosyslog_light.syslog_ng_config.statements.sources.opentelemetry_source import OTelResourceScopeLog
from axosyslog_light.syslog_ng_ctl.prometheus_stats_handler import MetricFilter

MESSAGE = "test message"
DISK_BUFFERS = [
    {"reliable": "yes", "dir": "'.'", "capacity-bytes": "1GiB"},
    {"reliable": "no", "dir": "'.'", "capacity-bytes": "1MiB"},
]
DISK_BUFFER_IDS = ["reliable_with_room_above_the_flow_control_window", "non_reliable_front_cache"]
FILTERX = r"$MSG = otel_logrecord(${.otel_raw.log}).body;"


def _output_events(config, result):
    samples = config.get_prometheus_samples([MetricFilter("syslogng_output_events_total", {"result": result})])
    return sum(sample.value for sample in samples)


def _export_in_background(opentelemetry_source, logs=1, body=MESSAGE):
    executor = ThreadPoolExecutor(max_workers=1)
    request = [OTelResourceScopeLog(log=OTelLog(body=body)) for _ in range(logs)]
    response = executor.submit(opentelemetry_source.write_logs, request)
    executor.shutdown(wait=False)
    return response


def _opentelemetry_to_network(config, port_allocator, flow_control=True, source_options=None, **destination_options):
    opentelemetry_source = config.create_opentelemetry_source(port=port_allocator(), **(source_options or {}))
    filterx = config.create_filterx(FILTERX)
    network_destination = config.create_network_destination(
        ip="localhost", port=port_allocator(), time_reopen=1, **destination_options,
    )
    config.create_logpath(
        statements=[opentelemetry_source, filterx, network_destination],
        flags="flow-control" if flow_control else None,
    )
    return opentelemetry_source, network_destination


def test_opentelemetry_source_responds_after_delivery(config, syslog_ng, port_allocator):
    opentelemetry_source, network_destination = _opentelemetry_to_network(config, port_allocator)
    syslog_ng.start(config)

    response = _export_in_background(opentelemetry_source)
    assert wait_until_true(lambda: _output_events(config, "queued") == 1)
    assert not wait_until_true_custom(response.done, timeout=1)

    network_destination.start_listener()
    assert network_destination.read_until_logs([MESSAGE])
    response.result(timeout=DEFAULT_TIMEOUT)


@pytest.mark.parametrize("disk_buffer", DISK_BUFFERS, ids=DISK_BUFFER_IDS)
def test_opentelemetry_source_responds_at_the_disk_buffer_write(config, syslog_ng, port_allocator, disk_buffer):
    opentelemetry_source, network_destination = _opentelemetry_to_network(
        config, port_allocator, disk_buffer=disk_buffer,
    )
    syslog_ng.start(config)

    response = _export_in_background(opentelemetry_source)
    response.result(timeout=DEFAULT_TIMEOUT)

    network_destination.start_listener()
    assert network_destination.read_until_logs([MESSAGE])


def test_opentelemetry_source_responds_at_enqueue_without_flow_control(config, syslog_ng, port_allocator):
    opentelemetry_source, network_destination = _opentelemetry_to_network(
        config, port_allocator, flow_control=False,
    )
    syslog_ng.start(config)

    response = _export_in_background(opentelemetry_source)
    response.result(timeout=DEFAULT_TIMEOUT)

    network_destination.start_listener()
    assert network_destination.read_until_logs([MESSAGE])


@pytest.mark.parametrize(
    "logs, source_options",
    [(1, {}), (2, {"log_iw_size": 1})],
    ids=["parked_before_the_exit", "blocked_on_the_window_at_the_exit"],
)
def test_opentelemetry_source_responds_unavailable_at_shutdown(config, syslog_ng, port_allocator, logs, source_options):
    opentelemetry_source, _ = _opentelemetry_to_network(config, port_allocator, source_options=source_options)
    syslog_ng.start(config)

    response = _export_in_background(opentelemetry_source, logs=logs)
    assert wait_until_true(lambda: _output_events(config, "queued") == 1)

    stop_started = time.monotonic()
    syslog_ng.stop()
    assert time.monotonic() - stop_started < 10

    with pytest.raises(grpc.RpcError) as error:
        response.result(timeout=DEFAULT_TIMEOUT)
    assert error.value.code() == grpc.StatusCode.UNAVAILABLE


def test_opentelemetry_source_responds_unavailable_at_reload(config, syslog_ng, port_allocator):
    opentelemetry_source, network_destination = _opentelemetry_to_network(config, port_allocator)
    syslog_ng.start(config)

    response = _export_in_background(opentelemetry_source)
    assert wait_until_true(lambda: _output_events(config, "queued") == 1)

    syslog_ng.reload(config)
    with pytest.raises(grpc.RpcError) as error:
        response.result(timeout=DEFAULT_TIMEOUT)
    assert error.value.code() == grpc.StatusCode.UNAVAILABLE

    network_destination.start_listener()
    assert network_destination.read_until_logs([MESSAGE])
    opentelemetry_source.write_log(log=OTelLog(body=MESSAGE))
    assert network_destination.read_until_logs([MESSAGE])


def test_opentelemetry_source_responds_while_two_calls_are_parked(config, syslog_ng, port_allocator):
    opentelemetry_source = config.create_opentelemetry_source(port=port_allocator())
    network_destination = config.create_network_destination(ip="localhost", port=port_allocator(), time_reopen=1)
    file_destination = config.create_file_destination(file_name="output.log", template='"$MSG\\n"')
    config.create_logpath(
        statements=[opentelemetry_source, config.create_filterx(FILTERX + ' if ($MSG != "parked") { drop; };'), network_destination],
        flags="flow-control",
    )
    config.create_logpath(
        statements=[opentelemetry_source, config.create_filterx(FILTERX + ' if ($MSG == "parked") { drop; };'), file_destination],
    )
    syslog_ng.start(config)

    parked = [_export_in_background(opentelemetry_source, body="parked") for _ in range(2)]
    assert wait_until_true(lambda: _output_events(config, "queued") == 2)

    _export_in_background(opentelemetry_source, body="served").result(timeout=DEFAULT_TIMEOUT)
    assert file_destination.read_log() == "served"

    network_destination.start_listener()
    assert network_destination.read_until_logs(["parked", "parked"])
    for response in parked:
        response.result(timeout=DEFAULT_TIMEOUT)


def test_opentelemetry_source_responds_ok_for_a_dropped_message(config, syslog_ng, port_allocator):
    opentelemetry_source = config.create_opentelemetry_source(port=port_allocator())
    network_destination = config.create_network_destination(ip="localhost", port=port_allocator(), time_reopen=1)
    config.create_logpath(
        statements=[
            opentelemetry_source, config.create_filterx(FILTERX),
            config.create_filter("message('matches-nothing')"), network_destination,
        ],
        flags="flow-control",
    )
    syslog_ng.start(config)

    _export_in_background(opentelemetry_source).result(timeout=DEFAULT_TIMEOUT)
