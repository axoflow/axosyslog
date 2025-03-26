#!/usr/bin/env python
#############################################################################
# Copyright (c) 2024 Axoflow
# Copyright (c) 2024 Attila Szakacs <attila.szakacs@axoflow.com>
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
from axosyslog_light.common.blocking import wait_until_true
from axosyslog_light.syslog_ng_config.renderer import render_statement
from axosyslog_light.syslog_ng_ctl.prometheus_stats_handler import MetricFilter


def create_config(config, port_allocator, filterx, stats_level=0):
    network_source = config.create_network_source(port=port_allocator(), flags="no-parse")
    file_destination = config.create_file_destination(file_name="output.log")
    raw_config = f"""
@version: {config.get_version()}

options {{ stats(level({stats_level})); }};

source s_network {{
    {render_statement(network_source)};
}};

destination d_file {{
    {render_statement(file_destination)};
}};

log {{
    source(s_network);
    filterx {{
        {filterx}
    }};
    destination(d_file);
}};
"""
    config.set_raw_config(raw_config)
    return network_source, file_destination


def test_filterx_update_metric_labels(config, port_allocator, syslog_ng):
    network_source, file_destination = create_config(
        config,
        port_allocator,
        r"""
            update_metric("literal", labels={"msg": $MSG, "foo": "foovalue"});

            labels = json();
            labels.msg = $MSG;
            labels.foo = "foovalue";
            update_metric("non_literal", labels=labels);
        """,
    )

    syslog_ng.start(config)
    network_source.write_log("msg1\nmsg2\nmsg1\nmsg3\n")
    file_destination.read_logs(4)

    # literal

    samples = config.get_prometheus_samples([MetricFilter("syslogng_literal", {})])
    assert len(samples) == 3

    samples = config.get_prometheus_samples([MetricFilter("syslogng_literal", {"msg": "msg1", "foo": "foovalue"})])
    assert len(samples) == 1
    assert int(samples[0].value) == 2

    samples = config.get_prometheus_samples([MetricFilter("syslogng_literal", {"msg": "msg2", "foo": "foovalue"})])
    assert len(samples) == 1
    assert int(samples[0].value) == 1

    samples = config.get_prometheus_samples([MetricFilter("syslogng_literal", {"msg": "msg3", "foo": "foovalue"})])
    assert len(samples) == 1
    assert int(samples[0].value) == 1

    # non literal

    samples = config.get_prometheus_samples([MetricFilter("syslogng_non_literal", {})])
    assert len(samples) == 3

    samples = config.get_prometheus_samples([MetricFilter("syslogng_non_literal", {"msg": "msg1", "foo": "foovalue"})])
    assert len(samples) == 1
    assert int(samples[0].value) == 2

    samples = config.get_prometheus_samples([MetricFilter("syslogng_non_literal", {"msg": "msg2", "foo": "foovalue"})])
    assert len(samples) == 1
    assert int(samples[0].value) == 1

    samples = config.get_prometheus_samples([MetricFilter("syslogng_non_literal", {"msg": "msg3", "foo": "foovalue"})])
    assert len(samples) == 1
    assert int(samples[0].value) == 1

    syslog_ng.stop()


def test_filterx_update_metric_increment(config, port_allocator, syslog_ng):
    network_source, file_destination = create_config(
        config,
        port_allocator,
        r"""
            update_metric("const", increment=3);
            # TODO: fix int cast from message_value
            update_metric("expr", increment=int(string($MSG)));
        """,
    )

    syslog_ng.start(config)
    network_source.write_logs(["3", "2", "1", "0"])
    file_destination.read_logs(4)

    samples = config.get_prometheus_samples([MetricFilter("syslogng_const", {})])
    assert len(samples) == 1
    assert int(samples[0].value) == 12

    samples = config.get_prometheus_samples([MetricFilter("syslogng_expr", {})])
    assert len(samples) == 1
    assert int(samples[0].value) == 6

    syslog_ng.stop()


def test_filterx_update_metric_level(config, port_allocator, syslog_ng):

    # stats(level(0));

    network_source, file_destination = create_config(
        config,
        port_allocator,
        r"""
            update_metric("metric", level=2);
        """,
        stats_level=0,
    )

    syslog_ng.start(config)
    network_source.write_log("foo")
    file_destination.read_logs(1)

    samples = config.get_prometheus_samples([MetricFilter("syslogng_metric", {})])
    assert len(samples) == 0

    # stats(level(1));

    network_source, file_destination = create_config(
        config,
        port_allocator,
        r"""
            update_metric("metric", level=2);
        """,
        stats_level=1,
    )

    syslog_ng.reload(config)
    network_source.write_log("foo")
    file_destination.read_logs(2)

    # stats(level(2));

    network_source, file_destination = create_config(
        config,
        port_allocator,
        r"""
            update_metric("metric", level=2);
        """,
        stats_level=2,
    )

    syslog_ng.reload(config)
    network_source.write_log("foo")
    file_destination.read_logs(3)

    assert wait_until_true(lambda: config.get_prometheus_samples([MetricFilter("syslogng_metric", {})]) != [])
    samples = config.get_prometheus_samples([MetricFilter("syslogng_metric", {})])
    assert len(samples) == 1
    assert int(samples[0].value) == 1

    syslog_ng.stop()
