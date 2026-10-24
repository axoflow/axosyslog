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
from concurrent.futures import ThreadPoolExecutor

from axosyslog_light.common.blocking import wait_until_true
from axosyslog_light.syslog_ng_config.statements.sources.opentelemetry_source import OTelLog
from axosyslog_light.syslog_ng_ctl.prometheus_stats_handler import MetricFilter


def _queued_events(config):
    samples = config.get_prometheus_samples([MetricFilter("syslogng_output_events_total", {"result": "queued"})])
    return sum(sample.value for sample in samples)


def test_opentelemetry_source_stops_while_flow_controlled_messages_are_queued(config, syslog_ng, port_allocator):
    opentelemetry_source = config.create_opentelemetry_source(port=port_allocator())
    network_destination = config.create_network_destination(ip="localhost", port=port_allocator(), time_reopen=1)
    config.create_logpath(statements=[opentelemetry_source, network_destination], flags="flow-control")
    syslog_ng.start(config)

    executor = ThreadPoolExecutor(max_workers=1)
    executor.submit(opentelemetry_source.write_log, log=OTelLog(body="test message"))
    executor.shutdown(wait=False)
    assert wait_until_true(lambda: _queued_events(config) == 1)

    syslog_ng.stop()
