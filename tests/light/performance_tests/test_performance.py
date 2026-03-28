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
import time
from pathlib import Path

import pytest
from config_helpers import build_performance_test_config
from generate_logs import generate_sample_log
from workers import LoggenWorker
from workers import run_and_measure_performance
from workers import SyslogNgWorker


logging.basicConfig(
    level=logging.DEBUG,
    format="%(asctime)s [%(levelname)s] %(message)s",
)
logger = logging.getLogger(__name__)
input_message_file_name = Path("input_message.txt")
file_dst_output_name = Path("file_dst_output.txt")


def start_syslog_ng(syslog_ng, config):
    syslog_ng.start_params.trace = True
    syslog_ng.start_params.debug = True
    syslog_ng.start_params.verbose = True
    syslog_ng.start_params.stderr = True
    syslog_ng.start(config)
    time.sleep(0.5)


def cleanup():
    input_message_file_name.unlink()
    try:
        file_dst_output_name.unlink()
    except Exception as e:
        logger.error(f"file destination does not exist: {file_dst_output_name}, error: {e}")


def generate_report(total_time, loggen_stats, syslog_ng_worker):
    total_delivered_msg = syslog_ng_worker.axosyslog_metrics[-1]["syslogng_output_events_total/delivered"][0].split()[1]
    total_delivered_msg_size = syslog_ng_worker.axosyslog_metrics[-1]["syslogng_output_event_bytes_total"][0].split()[1]

    logger.info(f"Total time taken: {total_time:.2f} seconds")
    logger.info(f"Total messages delivered: {total_delivered_msg}")
    logger.info(f"Throughput: {int(total_delivered_msg) / total_time:.2f} messages/second")
    logger.info(f"Throughput: {int(total_delivered_msg_size) / total_time / 1024 / 1024:.2f} MB/second")


def pytest_generate_tests(metafunc):
    if "input_msg_type" in metafunc.fixturenames:
        metafunc.parametrize("input_msg_type", metafunc.config.getoption("input_msg_type"))
    if "msg_size" in metafunc.fixturenames:
        metafunc.parametrize("msg_size", metafunc.config.getoption("msg_size"))
    if "msg_counter" in metafunc.fixturenames:
        metafunc.parametrize("msg_counter", [metafunc.config.getoption("msg_counter")])
    if "filterx_rule" in metafunc.fixturenames:
        metafunc.parametrize("filterx_rule", metafunc.config.getoption("filterx_rule"))
    if "destination" in metafunc.fixturenames:
        metafunc.parametrize("destination", metafunc.config.getoption("destination"))
    if "flow_control" in metafunc.fixturenames:
        metafunc.parametrize("flow_control", metafunc.config.getoption("flow_control"))
    if "run_time" in metafunc.fixturenames:
        metafunc.parametrize("run_time", [metafunc.config.getoption("run_time")])


@pytest.mark.asyncio
async def test_performance2(loggen, syslog_ng, syslog_ng_ctl, config, port_allocator, destination, filterx_rule, flow_control, clickhouse_server, clickhouse_ports, request, testcase_parameters, input_msg_type, msg_size, msg_counter, run_time):
    sample_log = generate_sample_log(input_msg_type, msg_size)
    config_start_params = {
        "destination": destination,
        "filterx_rule": filterx_rule,
        "flow_control": flow_control,
        "port_allocator": port_allocator,
        "clickhouse_server": clickhouse_server,
        "clickhouse_ports": clickhouse_ports,
    }
    config, source_driver, destination_driver = build_performance_test_config(config, config_start_params, request, testcase_parameters, sample_log)

    loggen_start_params = {
        "input_msg_type": input_msg_type,
        "msg_size": msg_size,
        "msg_counter": msg_counter,
        "run_time": run_time,
        "sample_log": sample_log,
        "source_driver": source_driver,
    }
    loggen_worker = LoggenWorker(loggen, loggen_start_params)

    syslog_ng_worker = SyslogNgWorker(syslog_ng_ctl, destination_driver)

    start_syslog_ng(syslog_ng, config)
    total_time = await run_and_measure_performance(loggen_worker, syslog_ng_worker)
    syslog_ng.stop()

    generate_report(total_time, loggen_worker.get_stats(), syslog_ng_worker)
    cleanup()
