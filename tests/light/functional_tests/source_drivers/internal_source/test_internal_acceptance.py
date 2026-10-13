#!/usr/bin/env python
#############################################################################
# Copyright (c) 2015-2021 Balabit
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
from axosyslog_light.syslog_ng_ctl.prometheus_stats_handler import MetricFilter


def test_internal_source_with_threaded_destination(config, syslog_ng):
    internal_source = config.create_internal_source()
    example_destination = config.create_example_destination(filename="output.txt")
    config.create_logpath(statements=[internal_source, example_destination])

    syslog_ng.start_params.stderr = False
    syslog_ng.start_params.trace = False
    syslog_ng.start_params.verbose = False

    syslog_ng.start(config)
    assert example_destination.read_until_logs(["syslog-ng starting up"])

    for _ in range(0, 5):
        syslog_ng.reload(config)
        assert example_destination.read_until_logs(["Configuration reload finished"])

    syslog_ng.stop()
    assert example_destination.read_until_logs(["syslog-ng shutting down"])


def test_internal_events_are_counted_by_severity(config, syslog_ng):
    internal_source = config.create_internal_source()
    internal_destination = config.create_file_destination(file_name="internal.log")
    config.create_logpath(statements=[internal_source, internal_destination])

    generator_source = config.create_example_msg_generator_source(num=1)
    unwritable_destination = config.create_file_destination(file_name="missing-dir/output.log", create_dirs="no")
    config.create_logpath(statements=[generator_source, unwritable_destination])

    syslog_ng.start_params.stderr = False
    syslog_ng.start_params.trace = False
    syslog_ng.start_params.verbose = False

    syslog_ng.start(config)
    assert internal_destination.read_until_logs(["syslog-ng starting up", "Error opening file for writing"])

    samples = config.get_prometheus_samples([MetricFilter("syslogng_internal_events_total", {})])
    by_severity = {sample.labels["severity"]: sample.value for sample in samples if "severity" in sample.labels}

    assert set(by_severity) == {"emerg", "alert", "crit", "err", "warning", "notice", "info", "debug"}
    assert by_severity["notice"] >= 1
    assert by_severity["err"] >= 1
