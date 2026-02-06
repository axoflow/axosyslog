#!/usr/bin/env python
#############################################################################
# Copyright (c) 2026 Axoflow
# Copyright (c) 2026 Andras Mitzki <andras.mitzki@axoflow.com>
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
import logging

import pytest
from axosyslog_light.common.blocking import wait_until_true
from axosyslog_light.helpers.loggen.loggen import Loggen
from axosyslog_light.helpers.loggen.loggen import LoggenStartParams
from axosyslog_light.syslog_ng_ctl.prometheus_stats_handler import MetricFilter


logger = logging.getLogger(__name__)

DEFAULT_LOG_IW_SIZE = 1000
DEFAULT_MAX_CONNECTIONS = 10
DEFAULT_INITIAL_WINDOW_SIZE = int(DEFAULT_LOG_IW_SIZE / DEFAULT_MAX_CONNECTIONS)
INPUT_WINDOW_CAPACITY = DEFAULT_INITIAL_WINDOW_SIZE * DEFAULT_MAX_CONNECTIONS
BIG_LOG_IW_SIZE = 987654321


def set_config_for_static_window(config, port_allocator, log_iw_size, max_connections):
    config.update_global_options(stats_level=5)
    network_source = config.create_network_source(ip="localhost", port=port_allocator(), log_iw_size=log_iw_size, max_connections=max_connections)
    network_destination = config.create_network_destination(ip="localhost", port=port_allocator(), time_reopen=1)
    config.create_logpath(statements=[network_source, network_destination], flags="flow-control")
    return config, network_source, network_destination


def set_config_for_dynamic_window(config, port_allocator, dynamic_window_size, log_iw_size):
    config.update_global_options(stats_level=5)
    network_source = config.create_network_source(ip="localhost", port=port_allocator(), log_iw_size=log_iw_size, max_connections=10, dynamic_window_size=dynamic_window_size, dynamic_window_realloc_ticks=1)
    network_destination = config.create_network_destination(ip="localhost", port=port_allocator(), time_reopen=1)
    config.create_logpath(statements=[network_source, network_destination], flags="flow-control")
    return config, network_source, network_destination


def send_messages_with_loggen(loggen, network_source, active_connections, message_counter):
    loggen.start(
        LoggenStartParams(
            target=network_source.options["ip"],
            port=network_source.options["port"],
            rate=10000,
            inet=True,
            active_connections=active_connections,
            number=message_counter,
        ),
    )
    assert wait_until_true(lambda: loggen.get_sent_message_count() == message_counter * active_connections)
    loggen.stop()


def wait_for_expected_prometheus_metric_values(config, metric_name, expected_value):
    def collect_metric_values(metric_name):
        metric_values = []
        for metric in config.get_prometheus_samples([MetricFilter(metric_name, {})]):
            metric_values.append(metric.value)
        logger.info(f"Current values for {metric_name}: {metric_values}")
        return sorted(metric_values)

    # phase 1: wait for metric in prometheus
    assert wait_until_true(lambda: len(config.get_prometheus_samples([MetricFilter(metric_name, {})])) > 0), \
        f"Metric {metric_name} is not available in prometheus"

    # phase 2: collect all metric values for a certain metric name until the expected value is reached
    assert wait_until_true(lambda: collect_metric_values(metric_name) == expected_value), \
        f"Metric {metric_name} expected: {expected_value}, actual: {collect_metric_values(metric_name)}"


def wait_for_sum_prometheus_metric_values(config, expected_metrics):
    def collect_metric_values(metric_name):
        metric_values = []
        for metric in config.get_prometheus_samples([MetricFilter(metric_name, {})]):
            metric_values.append(metric.value)
        logger.info(f"Current values for {metric_name}: {metric_values}")
        return sorted(metric_values)

    # phase 1: wait for metrics in prometheus
    for metric_name in expected_metrics.keys():
        assert wait_until_true(lambda: len(config.get_prometheus_samples([MetricFilter(metric_name, {})])) > 0), \
            f"Metric {metric_name} is not available in prometheus"

    # phase 2: collect all metric values for a certain metric name until the expected value is reached
    for metric_name, expected_value in expected_metrics.items():
        assert wait_until_true(lambda: sum(collect_metric_values(metric_name)) == expected_value), \
            f"Metric {metric_name} expected: {expected_value}, actual: {sum(collect_metric_values(metric_name))}"


def wait_for_prometheus_metric_above_expected(config, metric_name, expected_value):
    # check that the metric value is above the expected threshold value
    # it is not garanteed that the metric value will be exactly an expected value and
    # collected metric values can be different too
    def get_and_validate_metric_values():
        metric_values = []
        for metric in config.get_prometheus_samples([MetricFilter(metric_name, {})]):
            metric_values.append(metric.value)
        logger.info(f"Current values for {metric_name}: {metric_values}, expected threshold value: {expected_value}")
        return all(value > expected_value for value in metric_values)

    assert wait_until_true(lambda: get_and_validate_metric_values())


# Tests for static and dynamic window config options
@pytest.mark.parametrize(
    "log_iw_size, max_connections, invalid_syntax, small_window_size, expected_static_window_size", [
        (None, None, False, True, {"syslogng_input_window_available": 99, "syslogng_input_window_capacity": DEFAULT_INITIAL_WINDOW_SIZE}),  # set default values for both options
        (0, 1, True, False, None),
        (1, 0, True, False, None),
        (1, 1, False, True, {"syslogng_input_window_available": 99, "syslogng_input_window_capacity": DEFAULT_INITIAL_WINDOW_SIZE}),
        (BIG_LOG_IW_SIZE, 15, False, True, {"syslogng_input_window_available": int(BIG_LOG_IW_SIZE / 15) - 1, "syslogng_input_window_capacity": int(BIG_LOG_IW_SIZE / 15)}),
    ],
)
def test_static_window_config_options(config, syslog_ng, port_allocator, loggen, log_iw_size, max_connections, invalid_syntax, small_window_size, expected_static_window_size):
    config, network_source, _network_destination = set_config_for_static_window(config, port_allocator, log_iw_size, max_connections)

    if invalid_syntax:
        with pytest.raises(Exception) as exec_info:
            syslog_ng.start(config)
        assert "syslog-ng config syntax error" in str(exec_info.value)
        return

    syslog_ng.start(config)

    send_messages_with_loggen(loggen, network_source, 1, 1)
    wait_for_sum_prometheus_metric_values(config, expected_static_window_size)

    syslog_ng.stop()

    if small_window_size:
        syslog_ng.is_message_in_console_log("The result was too small, increasing to a reasonable minimum value")


@pytest.mark.parametrize(
    "dynamic_window_size, expected_static_window_size", [
        # log_iw_size=10, dynamic_window_size=0, max_connections=10, window_size=100 (per conn.) as the values are under min.
        (0, {"syslogng_input_window_available": (10 * 99), "syslogng_input_window_capacity": (10 * 100)}),
        # log_iw_size=10, dynamic_window_size=1, max_connections=10, window_size=1 (per conn.)
        (1, {"syslogng_input_window_available": (10 * 0), "syslogng_input_window_capacity": (10 * 1)}),
    ],
)
def test_dynamic_window_config_options(config, syslog_ng, port_allocator, loggen, dynamic_window_size, expected_static_window_size):
    config, network_source, _network_destination = set_config_for_dynamic_window(config, port_allocator, dynamic_window_size, log_iw_size=10)
    syslog_ng.start(config)

    send_messages_with_loggen(loggen, network_source, 10, 1)
    wait_for_sum_prometheus_metric_values(config, expected_static_window_size)


# Test for update static and dynamic window option values
def test_update_static_window_option_value(config, syslog_ng, port_allocator, loggen, syslog_ng_ctl):
    config, network_source, network_destination = set_config_for_static_window(config, port_allocator, log_iw_size=5000, max_connections=10)
    syslog_ng.start(config)

    # phase 1: check initial log_iw_size value
    send_messages_with_loggen(loggen, network_source, 10, 500 - 1)
    # we have sent 10 * 499 msgs
    # initial static window size is 500, available window will be 500-499=1
    wait_for_sum_prometheus_metric_values(config, {"syslogng_input_window_available": 10 * 1, "syslogng_input_window_capacity": 10 * 500})

    # phase 2: update log_iw_size value and check the new value is applied
    network_source.options["log_iw_size"] = 6000
    syslog_ng.reload(config)
    send_messages_with_loggen(loggen, network_source, 1, 1)
    # we have sent 1 * 1 msg,
    # static window size for new connection is 6000/10=600, available window for new connection 600-1=599
    wait_for_sum_prometheus_metric_values(config, {"syslogng_input_window_available": (10 * 1) + 599, "syslogng_input_window_capacity": (10 * 500) + 600})

    # phase 3: start destination and check that the window metrics are updated accordingly
    network_destination.start_listener()
    wait_for_sum_prometheus_metric_values(config, {"syslogng_input_window_available": (10 * 500) + 600, "syslogng_input_window_capacity": (10 * 500) + 600})


def test_update_dynamic_window_option_value(config, syslog_ng, port_allocator, loggen, syslog_ng_ctl):
    config, network_source, network_destination = set_config_for_dynamic_window(config, port_allocator, dynamic_window_size=5000, log_iw_size=10)
    syslog_ng.start(config)

    # phase 1: check initial log_iw_size value
    send_messages_with_loggen(loggen, network_source, 10, 500 - 1)
    # we have sent 10 * 499 msgs,
    # initial dynamic window size is 5000/10 (dynamic part) + 10/10 (static part) = 501
    # due to loggen has exited dynamic window capacity will be as many log we have sent = 499
    # and the available window size will be 0, as the dynamic window is fully occupied
    wait_for_sum_prometheus_metric_values(config, {"syslogng_input_window_available": 10 * 0, "syslogng_input_window_capacity": 10 * 499})

    # phase 2: update log_iw_size value and check the new value is applied
    network_source.options["dynamic_window_size"] = 6000
    syslog_ng.reload(config)
    send_messages_with_loggen(loggen, network_source, 1, 1)
    # we have sent 1 * 1 msg
    # dynamic window capacity for new connection is 1, as loggen has exited
    # available window size is 0, as the dynamic window is fully occupied
    wait_for_sum_prometheus_metric_values(config, {"syslogng_input_window_available": (10 * 0) + 0, "syslogng_input_window_capacity": (10 * 499) + 1})

    # phase 3: start destination and check that the window metrics are updated accordingly
    network_destination.start_listener()
    wait_for_sum_prometheus_metric_values(config, {"syslogng_input_window_available": (10 * 1) + 1, "syslogng_input_window_capacity": (10 * 1) + 1})


# Tests for dynamic window rebalance
def test_dynamic_window_rebalance_with_paralell_connections(config, syslog_ng, port_allocator, syslog_ng_ctl):
    config, network_source, network_destination = set_config_for_dynamic_window(config, port_allocator, dynamic_window_size=5000, log_iw_size=1000)
    static_window_size = int(network_source.options["log_iw_size"] / network_source.options["max_connections"])
    syslog_ng.start(config)

    # phase 1: start first connection with slower sending rate
    loggen1 = Loggen()
    loggen1.start(LoggenStartParams(target=network_source.options["ip"], port=network_source.options["port"], inet=True, stream=True, rate=5, interval=5))

    # phase 2: start second connection with faster sending rate
    loggen2 = Loggen()
    loggen2.start(LoggenStartParams(target=network_source.options["ip"], port=network_source.options["port"], inet=True, stream=True, rate=100, interval=5))
    wait_for_prometheus_metric_above_expected(config, "syslogng_input_window_capacity", static_window_size)

    # phase 3: because the loggen connections are already closed, the window capacity should go back to the static size after processing all logs.
    network_destination.start_listener()
    wait_for_expected_prometheus_metric_values(config, "syslogng_input_window_capacity", [static_window_size, static_window_size])
    wait_for_expected_prometheus_metric_values(config, "syslogng_input_window_available", [static_window_size, static_window_size])


# Test for increasing message number until the window is full
def test_static_window_increased_send(config, syslog_ng, port_allocator, loggen, syslog_ng_ctl):
    config, network_source, network_destination = set_config_for_static_window(config, port_allocator, log_iw_size=1000, max_connections=10)
    syslog_ng.start(config)

    sum_msg_counter = 0
    sum_input_window_capacity = []
    sum_input_window_available = []
    window_size = 1000 / 10  # per connection
    for msg_counter in range(10, 100 + 10, 10):
        # sent msgs in order: 10, 20, 30 ... 100
        send_messages_with_loggen(loggen, network_source, 1, msg_counter)

        sum_msg_counter += msg_counter
        sum_input_window_capacity.append(window_size)
        available_window = window_size - msg_counter
        if available_window < 0:
            available_window = 0
        sum_input_window_available.append(available_window)
        wait_for_expected_prometheus_metric_values(config, "syslogng_input_window_available", sorted(sum_input_window_available))
        wait_for_expected_prometheus_metric_values(config, "syslogng_input_window_capacity", sorted(sum_input_window_capacity))
        wait_for_sum_prometheus_metric_values(config, {"syslogng_input_events_total": sum_msg_counter})

    network_destination.start_listener()
    wait_for_sum_prometheus_metric_values(config, {"syslogng_input_window_available": (10 * 100), "syslogng_input_window_capacity": (10 * 100)})


def test_dynamic_window_increased_send(config, syslog_ng, port_allocator, loggen):
    config, network_source, network_destination = set_config_for_dynamic_window(config, port_allocator, dynamic_window_size=5000, log_iw_size=10)
    syslog_ng.start(config)

    sum_msg_counter = 0
    sum_input_window_capacity = []
    input_window_available = []
    dynamic_window_size = (5000 / 10) + (10 / 10)  # per connection
    dynamic_window_for_all_connections = dynamic_window_size * 10
    for msg_counter in range(100, 1000 + 100, 100):
        # sent msgs in order: 100, 200, 300 ... 1000
        send_messages_with_loggen(loggen, network_source, 1, msg_counter)

        sum_msg_counter += msg_counter
        if sum_msg_counter > dynamic_window_for_all_connections:
            sum_msg_counter = dynamic_window_for_all_connections
            sum_input_window_capacity.append(510)
        else:
            sum_input_window_capacity.append(msg_counter)
        input_window_available.append(0)

        wait_for_expected_prometheus_metric_values(config, "syslogng_input_window_available", sorted(input_window_available))
        wait_for_expected_prometheus_metric_values(config, "syslogng_input_window_capacity", sorted(sum_input_window_capacity))
        wait_for_sum_prometheus_metric_values(config, {"syslogng_input_events_total": sum_msg_counter})

    network_destination.start_listener()
    wait_for_sum_prometheus_metric_values(config, {"syslogng_input_window_available": (10 * 1), "syslogng_input_window_capacity": (10 * 1)})


# Tests for filling up window buffers and max connections
def test_fill_static_window_for_all_source_connections(config, syslog_ng, port_allocator, loggen, syslog_ng_ctl):
    used_source_connections = 10
    config, network_source, network_destination = set_config_for_static_window(config, port_allocator, log_iw_size=1000, max_connections=used_source_connections)
    static_window_size = int(network_source.options["log_iw_size"] / network_source.options["max_connections"])  # per connection

    syslog_ng.start(config)

    # phase 1: fill up window buffers for all connections and check
    send_messages_with_loggen(loggen, network_source, used_source_connections, static_window_size)
    wait_for_expected_prometheus_metric_values(config, "syslogng_input_window_available", used_source_connections * [0])
    wait_for_expected_prometheus_metric_values(config, "syslogng_input_window_capacity", used_source_connections * [100])
    assert syslog_ng.count_message_in_console_log("Source has been suspended") == used_source_connections

    # phase 2: send additional messages and check that the new connections are rejected
    assert not syslog_ng.is_message_in_console_log("Number of allowed concurrent connections reached, rejecting connection")
    send_messages_with_loggen(loggen, network_source, 1, 1)
    assert syslog_ng.is_message_in_console_log("Number of allowed concurrent connections reached, rejecting connection")

    network_destination.start_listener()
    wait_for_sum_prometheus_metric_values(
        config,
        {
            "syslogng_input_window_available": (used_source_connections * static_window_size),
            "syslogng_input_window_capacity": (used_source_connections * static_window_size),
        },
    )


def test_fill_dynamic_window_for_all_source_connections(config, syslog_ng, port_allocator, loggen, syslog_ng_ctl):
    used_source_connections = 10
    config, network_source, network_destination = set_config_for_dynamic_window(config, port_allocator, dynamic_window_size=2000, log_iw_size=10)
    dynamic_window_size = (network_source.options["dynamic_window_size"] / network_source.options["max_connections"]) + \
        (network_source.options["log_iw_size"] / network_source.options["max_connections"])  # per connection

    syslog_ng.start(config)
    # phase 1: fill up window buffers for all connections and check
    send_messages_with_loggen(loggen, network_source, used_source_connections, int(dynamic_window_size))
    wait_for_expected_prometheus_metric_values(config, "syslogng_input_window_available", used_source_connections * [0])
    wait_for_expected_prometheus_metric_values(config, "syslogng_input_window_capacity", used_source_connections * [int(dynamic_window_size)])
    assert syslog_ng.count_message_in_console_log("Source has been suspended") == used_source_connections

    # phase 2: send additional messages and check that the new connections are rejected
    assert not syslog_ng.is_message_in_console_log("Number of allowed concurrent connections reached, rejecting connection")
    send_messages_with_loggen(loggen, network_source, 1, 1)
    assert syslog_ng.is_message_in_console_log("Number of allowed concurrent connections reached, rejecting connection")

    network_destination.start_listener()
    wait_for_sum_prometheus_metric_values(
        config,
        {
            "syslogng_input_window_available": (used_source_connections * 1),
            "syslogng_input_window_capacity": (used_source_connections * 1),
        },
    )
