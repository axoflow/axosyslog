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
import time

import pytest
from axosyslog_light.common.blocking import wait_until_true
from axosyslog_light.helpers.loggen.loggen import LoggenStartParams
from axosyslog_light.syslog_ng_ctl.prometheus_stats_handler import MetricFilter

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


def send_messages_per_connection(loggen, network_source, active_connections, message_counter):
    for i in range(active_connections):
        send_messages_with_loggen(loggen, network_source, 1, message_counter)


def send_messages_for_all_connections(loggen, network_source, active_connections, message_counter):
    send_messages_with_loggen(loggen, network_source, active_connections, message_counter)


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
    wait_until_true(lambda: loggen.get_sent_message_count() == message_counter * active_connections)
    loggen.stop()
    time.sleep(2)


def check_statistics(network_destination, expected_stats):
    for stat_name, expected_value in expected_stats.items():
        assert wait_until_true(lambda: stat_name in network_destination.get_stats() and network_destination.get_stats()[stat_name] == expected_value), "Expected stat {} not found in stats".format(stat_name)


def check_prometheus_metrics(config, expected_metrics):
    for metric_name, expected_value in expected_metrics.items():
        sum_values = 0
        all_metrics = config.get_prometheus_samples([MetricFilter(metric_name, {})])
        for metric in all_metrics:
            sum_values += metric.value
        assert sum_values == expected_value, f"Metric {metric_name} expected: {expected_value}, actual: {sum_values}"


def fill_up_window_buffers_one_by_one_for_all_connections_and_check(config, syslog_ng, loggen, network_source, network_destination):
    config_log_iw_size = network_source.options["log_iw_size"]
    config_max_connections = network_source.options["max_connections"]
    initial_window_size = int(config_log_iw_size / config_max_connections)
    all_message_counter_for_all_connections = initial_window_size * config_max_connections

    send_messages_per_connection(loggen, network_source, config_max_connections, initial_window_size)
    check_statistics(network_destination, {"processed": all_message_counter_for_all_connections, "dropped": 0, "queued": all_message_counter_for_all_connections, "written": 0})
    check_prometheus_metrics(config, {"syslogng_input_window_available": 0, "syslogng_input_window_capacity": all_message_counter_for_all_connections})
    assert syslog_ng.count_message_in_console_log("Source has been suspended") == config_max_connections


def fill_up_window_buffers_at_once_for_all_connections_and_check(config, syslog_ng, loggen, network_source, network_destination):
    config_log_iw_size = network_source.options["log_iw_size"]
    config_max_connections = network_source.options["max_connections"]
    initial_window_size = int(config_log_iw_size / config_max_connections)
    all_message_counter_for_all_connections = initial_window_size * config_max_connections

    send_messages_for_all_connections(loggen, network_source, config_max_connections, initial_window_size)
    check_statistics(network_destination, {"processed": all_message_counter_for_all_connections, "dropped": 0, "queued": all_message_counter_for_all_connections, "written": 0})
    check_prometheus_metrics(config, {"syslogng_input_window_available": 0, "syslogng_input_window_capacity": all_message_counter_for_all_connections})
    assert syslog_ng.count_message_in_console_log("Source has been suspended") == config_max_connections


def send_additional_one_message_and_check(loggen, network_source, syslog_ng):
    assert not syslog_ng.is_message_in_console_log("Number of allowed concurrent connections reached, rejecting connection")
    send_messages_with_loggen(loggen, network_source, 1, 1)
    assert syslog_ng.is_message_in_console_log("Number of allowed concurrent connections reached, rejecting connection")


def start_destination_and_check(config, network_destination):
    network_destination.start_listener()
    assert network_destination.read_logs(counter=INPUT_WINDOW_CAPACITY)
    check_statistics(
        network_destination, {
            "processed": INPUT_WINDOW_CAPACITY,
            "dropped": 0,
            "queued": 0,
            "written": INPUT_WINDOW_CAPACITY,
        },
    )
    check_prometheus_metrics(config, {"syslogng_input_window_available": INPUT_WINDOW_CAPACITY, "syslogng_input_window_capacity": INPUT_WINDOW_CAPACITY})


def fill_up_all_window_buffer_for_first_connection_and_check(loggen, network_source, config):
    send_messages_with_loggen(loggen, network_source, 1, 300)
    check_prometheus_metrics(config, {"syslogng_input_window_available": 0, "syslogng_input_window_capacity": 201})


def check_rebalance_with_sending_new_logs_with_second_connection_and_check(loggen, network_source, config):
    send_messages_with_loggen(loggen, network_source, 1, 300)
    check_prometheus_metrics(config, {"syslogng_input_window_available": 0, "syslogng_input_window_capacity": 201})


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
    config, network_source, network_destination = set_config_for_static_window(config, port_allocator, log_iw_size, max_connections)

    if invalid_syntax:
        with pytest.raises(Exception) as exec_info:
            syslog_ng.start(config)
        assert "syslog-ng config syntax error" in str(exec_info.value)
        return

    syslog_ng.start(config)

    send_messages_with_loggen(loggen, network_source, 1, 1)
    check_prometheus_metrics(config, expected_static_window_size)

    syslog_ng.stop()

    if small_window_size:
        syslog_ng.is_message_in_console_log("The result was too small, increasing to a reasonable minimum value")


def test_fill_source_connections_static_window_one_by_one(config, syslog_ng, port_allocator, loggen, syslog_ng_ctl):
    config, network_source, network_destination = set_config_for_static_window(config, port_allocator, DEFAULT_LOG_IW_SIZE, DEFAULT_MAX_CONNECTIONS)

    syslog_ng.start(config)
    fill_up_window_buffers_one_by_one_for_all_connections_and_check(config, syslog_ng, loggen, network_source, network_destination)

    send_additional_one_message_and_check(loggen, network_source, syslog_ng)
    start_destination_and_check(config, network_destination)


def test_fill_source_connections_static_window_all_at_once(config, syslog_ng, port_allocator, loggen, syslog_ng_ctl):
    config, network_source, network_destination = set_config_for_static_window(config, port_allocator, DEFAULT_LOG_IW_SIZE, DEFAULT_MAX_CONNECTIONS)

    syslog_ng.start(config)
    fill_up_window_buffers_at_once_for_all_connections_and_check(config, syslog_ng, loggen, network_source, network_destination)

    send_additional_one_message_and_check(loggen, network_source, syslog_ng)
    start_destination_and_check(config, network_destination)


def test_increase_log_iw_size_value_during_run(config, syslog_ng, port_allocator, loggen, syslog_ng_ctl):
    config_log_iw_size = 5000
    config_max_connections = 10
    window_size = int(config_log_iw_size / config_max_connections)

    config, network_source, network_destination = set_config_for_static_window(config, port_allocator, config_log_iw_size, config_max_connections)

    syslog_ng.start(config)
    send_messages_with_loggen(loggen, network_source, config_max_connections, window_size - 1)
    check_prometheus_metrics(config, {"syslogng_input_window_available": config_max_connections * (window_size - (window_size - 1)), "syslogng_input_window_capacity": config_max_connections * window_size})

    config_new_log_iw_size = 6000
    network_source.options["log_iw_size"] = config_new_log_iw_size
    syslog_ng.reload(config)
    send_messages_with_loggen(loggen, network_source, 1, 1)
    check_prometheus_metrics(config, {"syslogng_input_window_available": (10 * 1) + 599, "syslogng_input_window_capacity": (10 * 500) + 600})


def test_decrease_log_iw_size_value_during_run(config, syslog_ng, port_allocator, loggen, syslog_ng_ctl):
    config_log_iw_size = 5000
    config_max_connections = 10
    window_size = int(config_log_iw_size / config_max_connections)

    config, network_source, network_destination = set_config_for_static_window(config, port_allocator, config_log_iw_size, config_max_connections)

    syslog_ng.start(config)
    send_messages_with_loggen(loggen, network_source, config_max_connections, window_size - 1)
    check_prometheus_metrics(config, {"syslogng_input_window_available": config_max_connections * (window_size - (window_size - 1)), "syslogng_input_window_capacity": config_max_connections * window_size})

    config_new_log_iw_size = 4000
    network_source.options["log_iw_size"] = config_new_log_iw_size
    syslog_ng.reload(config)
    send_messages_with_loggen(loggen, network_source, 1, 1)
    check_prometheus_metrics(config, {"syslogng_input_window_available": (10 * 1) + 399, "syslogng_input_window_capacity": (10 * 500) + 400})


@pytest.mark.parametrize(
    "dynamic_window_size, expected_static_window_size", [
        (0, {"syslogng_input_window_available": (10 * 99), "syslogng_input_window_capacity": (10 * 100)}),  # log_iw_size=1, dynamic_window_size=0, max_connections=10, window_size=100 (per conn.) as the values are under min.
        (1, {"syslogng_input_window_available": (10 * 1), "syslogng_input_window_capacity": (10 * 2)}),  # log_iw_size=1, dynamic_window_size=1, max_connections=10, window_size=2 (per conn.)
        (5000, {"syslogng_input_window_available": (10 * 500), "syslogng_input_window_capacity": (10 * 501)}),   # log_iw_size=1, dynamic_window_size=5000, max_connections=10, window_size=501 (per conn.)
    ],
)
def test_dynamic_window_size_config_options(config, syslog_ng, port_allocator, loggen, dynamic_window_size, expected_static_window_size):
    config, network_source, network_destination = set_config_for_dynamic_window(config, port_allocator, dynamic_window_size, log_iw_size=1)

    syslog_ng.start(config)

    send_messages_with_loggen(loggen, network_source, 10, 1)
    check_prometheus_metrics(config, expected_static_window_size)

    syslog_ng.stop()


def test_dynamic_window_size_rebalance(config, syslog_ng, port_allocator, loggen):
    config, network_source, network_destination = set_config_for_dynamic_window(config, port_allocator, dynamic_window_size=200, log_iw_size=1)
    syslog_ng.start(config)
    fill_up_all_window_buffer_for_first_connection_and_check(loggen, network_source, config)
    check_rebalance_with_sending_new_logs_with_second_connection_and_check(loggen, network_source, config)


def test_static_window_size_increased_send(config, syslog_ng, port_allocator, loggen, syslog_ng_ctl):
    config, network_source, network_destination = set_config_for_dynamic_window(config, port_allocator, dynamic_window_size=0, log_iw_size=1000)
    syslog_ng.start(config)
    sum_msg_counter = 0
    sum_input_window_capacity = 0
    sum_input_window_available = 0
    for msg_counter in range(10, 100 + 10, 10):
        sum_msg_counter += msg_counter
        sum_input_window_capacity += 100
        sum_input_window_available += 100 - msg_counter
        send_messages_with_loggen(loggen, network_source, 1, msg_counter)
        check_prometheus_metrics(
            config, {
                "syslogng_input_window_available": sum_input_window_available,
                "syslogng_input_window_capacity": sum_input_window_capacity,
                "syslogng_input_events_total": sum_msg_counter,
            },
        )
    start_destination_and_check(config, network_destination)


def test_dynamic_window_size_increased_send(config, syslog_ng, port_allocator, loggen, syslog_ng_ctl):
    config, network_source, network_destination = set_config_for_dynamic_window(config, port_allocator, dynamic_window_size=5000, log_iw_size=1)
    syslog_ng.start(config)

    sum_msg_counter = 0
    # sum_input_window_capacity = 0
    # sum_input_window_available = 0
    for msg_counter in range(100, 1000 + 100, 100):
        sum_msg_counter += msg_counter
        # sum_input_window_capacity += 100
        # sum_input_window_available += 100-msg_counter
        send_messages_with_loggen(loggen, network_source, 1, msg_counter)
        check_prometheus_metrics(
            config, {
                # "syslogng_input_window_available": sum_input_window_available,
                # "syslogng_input_window_capacity": sum_input_window_capacity,
                "syslogng_input_events_total": sum_msg_counter,
            },
        )

    start_destination_and_check(config, network_destination)
