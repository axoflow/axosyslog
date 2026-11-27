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
import socket
from concurrent.futures import ThreadPoolExecutor

import pytest
from axosyslog_light.common.blocking import DEFAULT_TIMEOUT
from axosyslog_light.common.blocking import wait_until_true
from axosyslog_light.common.blocking import wait_until_true_custom
from axosyslog_light.syslog_ng_ctl.prometheus_stats_handler import MetricFilter

MESSAGE = "test message"
DISK_BUFFERS = [
    {"reliable": "yes", "dir": "'.'", "capacity-bytes": "1GiB"},
    {"reliable": "no", "dir": "'.'", "capacity-bytes": "1MiB"},
]
DISK_BUFFER_IDS = ["reliable_with_room_above_the_flow_control_window", "non_reliable_front_cache"]


def _output_events(config, result):
    samples = config.get_prometheus_samples([MetricFilter("syslogng_output_events_total", {"result": result})])
    return sum(sample.value for sample in samples)


def _send_in_background(send, *args):
    executor = ThreadPoolExecutor(max_workers=1)
    response = executor.submit(send, *args)
    executor.shutdown(wait=False)
    return response


def _http_post(message):
    return f"POST / HTTP/1.1\r\nHost: localhost\r\nContent-Length: {len(message)}\r\n\r\n{message}".encode()


def _recv_responses(sock, count, status=b"200 OK"):
    status_line = b"HTTP/1.1 " + status
    data = b""
    while data.count(status_line) < count:
        chunk = sock.recv(4096)
        if not chunk:
            raise Exception(f"connection closed after {data.count(status_line)} of {count} responses")
        data += chunk
    return data


def _ehttp_to_network(config, port_allocator, flow_control=True, source_options=None, **destination_options):
    ehttp_source = config.create_ehttp_source(port=port_allocator(), flags="no-parse", **(source_options or {}))
    network_destination = config.create_network_destination(
        ip="localhost", port=port_allocator(), time_reopen=1, **destination_options,
    )
    config.create_logpath(
        statements=[ehttp_source, network_destination], flags="flow-control" if flow_control else None,
    )
    return ehttp_source, network_destination


def test_ehttp_source_responds_after_delivery(config, syslog_ng, port_allocator):
    ehttp_source, network_destination = _ehttp_to_network(config, port_allocator)
    syslog_ng.start(config)

    response = _send_in_background(ehttp_source.write_log, MESSAGE)
    assert wait_until_true(lambda: _output_events(config, "queued") == 1)
    assert not wait_until_true_custom(response.done, timeout=1)

    network_destination.start_listener()
    assert network_destination.read_until_logs([MESSAGE])
    response.result(timeout=DEFAULT_TIMEOUT)


@pytest.mark.parametrize("disk_buffer", DISK_BUFFERS, ids=DISK_BUFFER_IDS)
def test_ehttp_source_responds_at_the_disk_buffer_write(config, syslog_ng, port_allocator, disk_buffer):
    ehttp_source, network_destination = _ehttp_to_network(config, port_allocator, disk_buffer=disk_buffer)
    syslog_ng.start(config)

    response = _send_in_background(ehttp_source.write_log, MESSAGE)
    response.result(timeout=DEFAULT_TIMEOUT)

    network_destination.start_listener()
    assert network_destination.read_until_logs([MESSAGE])


def test_ehttp_source_responds_at_enqueue_without_flow_control(config, syslog_ng, port_allocator):
    ehttp_source, network_destination = _ehttp_to_network(config, port_allocator, flow_control=False)
    syslog_ng.start(config)

    response = _send_in_background(ehttp_source.write_log, MESSAGE)
    response.result(timeout=DEFAULT_TIMEOUT)

    network_destination.start_listener()
    assert network_destination.read_until_logs([MESSAGE])


def test_ehttp_source_responds_in_order_on_a_keep_alive_connection(config, syslog_ng, port_allocator):
    ehttp_source, network_destination = _ehttp_to_network(config, port_allocator)
    syslog_ng.start(config)

    with socket.create_connection(("localhost", ehttp_source.options["port"])) as sock:
        sock.sendall(_http_post("first") + _http_post("second"))
        assert wait_until_true(lambda: _output_events(config, "queued") == 1)
        sock.settimeout(1)
        with pytest.raises(socket.timeout):
            sock.recv(4096)

        network_destination.start_listener()
        assert network_destination.read_until_logs(["first", "second"])
        sock.settimeout(DEFAULT_TIMEOUT)
        _recv_responses(sock, 2)


def test_ehttp_source_responds_on_a_new_connection_after_a_client_left_while_parked(config, syslog_ng, port_allocator):
    ehttp_source, network_destination = _ehttp_to_network(config, port_allocator)
    syslog_ng.start(config)

    with socket.create_connection(("localhost", ehttp_source.options["port"])) as sock:
        sock.sendall(_http_post("left"))
        assert wait_until_true(lambda: _output_events(config, "queued") == 1)

    network_destination.start_listener()
    assert network_destination.read_until_logs(["left"])
    assert syslog_ng.is_process_running()

    ehttp_source.write_log(MESSAGE)
    assert network_destination.read_until_logs([MESSAGE])


def test_ehttp_source_responds_after_the_last_ack_of_a_request_larger_than_the_window(config, syslog_ng, port_allocator):
    config.update_global_options(min_iw_size_per_reader=1)
    ehttp_source, network_destination = _ehttp_to_network(
        config, port_allocator, source_options={"log_iw_size": 2, "max_connections": 1},
    )
    syslog_ng.start(config)

    response = _send_in_background(ehttp_source.write_log, "first\nsecond\nthird")
    assert wait_until_true(lambda: _output_events(config, "queued") == 2)
    assert not wait_until_true_custom(response.done, timeout=1)

    network_destination.start_listener()
    assert network_destination.read_until_logs(["first", "second", "third"])
    response.result(timeout=DEFAULT_TIMEOUT)


def test_ehttp_source_responds_503_when_the_ack_is_aborted(config, syslog_ng, port_allocator):
    ehttp_source, network_destination = _ehttp_to_network(config, port_allocator)
    syslog_ng.start(config)

    with socket.create_connection(("localhost", ehttp_source.options["port"])) as sock:
        sock.sendall(_http_post("aborted"))
        assert wait_until_true(lambda: _output_events(config, "queued") == 1)

        network_destination.options["port"] = port_allocator()
        syslog_ng.reload(config)

        sock.settimeout(DEFAULT_TIMEOUT)
        _recv_responses(sock, 1, status=b"503 Service Unavailable")

    network_destination.start_listener()
    ehttp_source.write_log(MESSAGE)
    assert network_destination.read_until_logs([MESSAGE])


def test_ehttp_source_responds_ok_for_a_dropped_message(config, syslog_ng, port_allocator):
    ehttp_source = config.create_ehttp_source(port=port_allocator(), flags="no-parse")
    network_destination = config.create_network_destination(ip="localhost", port=port_allocator(), time_reopen=1)
    config.create_logpath(
        statements=[ehttp_source, config.create_filter("message('matches-nothing')"), network_destination],
        flags="flow-control",
    )
    syslog_ng.start(config)

    _send_in_background(ehttp_source.write_log, MESSAGE).result(timeout=DEFAULT_TIMEOUT)
