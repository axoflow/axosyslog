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

from axosyslog_light.common.blocking import wait_until_true
from axosyslog_light.syslog_ng_ctl.prometheus_stats_handler import MetricFilter

METRIC_NAME = "syslogng_output_connection_failures_total"


def _get_samples(config, reason):
    return config.get_prometheus_samples([MetricFilter(METRIC_NAME, {"reason": reason})])


def _get_metric_value(config, reason):
    return sum(s.value for s in _get_samples(config, reason))


def _create_config(config, port):
    generator_source = config.create_example_msg_generator_source(num=1, template=config.stringify("message text"))
    network_destination = config.create_network_destination(ip="127.0.0.1", port=port, transport="tcp", time_reopen=1)
    config.create_logpath(statements=[generator_source, network_destination])


def test_refused_connection_is_counted(config, syslog_ng, port_allocator):
    port = port_allocator()
    _create_config(config, port)

    syslog_ng.start(config)

    assert wait_until_true(lambda: _get_metric_value(config, "refused") >= 1)

    samples = _get_samples(config, "refused")
    assert len(samples) == 1
    assert samples[0].labels["driver"] == "network"
    assert samples[0].labels["transport"] == "tcp"
    assert samples[0].labels["address"] == "127.0.0.1:{}".format(port)


def test_connection_closed_by_the_peer_is_counted_as_eof(config, syslog_ng, port_allocator):
    port = port_allocator()
    _create_config(config, port)

    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", port))
    listener.listen(1)
    listener.settimeout(10)
    try:
        syslog_ng.start(config)

        connection, _ = listener.accept()
        connection.settimeout(10)
        received = b""
        while not received.endswith(b"\n"):
            chunk = connection.recv(4096)
            if not chunk:
                break
            received += chunk
        connection.close()

        assert b"message text" in received
        assert wait_until_true(lambda: _get_metric_value(config, "eof") >= 1)
    finally:
        listener.close()
