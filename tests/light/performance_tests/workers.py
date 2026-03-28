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
import asyncio
import logging
from contextlib import asynccontextmanager
from datetime import datetime
from pathlib import Path

from axosyslog_light.common.blocking import wait_until_true_custom
from axosyslog_light.helpers.loggen.loggen import LoggenStartParams


logger = logging.getLogger(__name__)
input_message_file_name = Path("input_message.txt")


class LoggenWorker:
    def __init__(self, loggen, loggen_start_params):
        self._task = None
        self._loggen = loggen
        self._input_msg_type = loggen_start_params["input_msg_type"]
        self._msg_size = loggen_start_params["msg_size"]
        self._msg_counter = loggen_start_params["msg_counter"]
        self._run_time = loggen_start_params["run_time"]
        self._sample_log = loggen_start_params["sample_log"]
        self._source_driver = loggen_start_params["source_driver"]
        logger.debug(
            f"LoggenWorker initialized with msg_type={self._input_msg_type}, "
            f"msg_size={self._msg_size}, msg_counter={self._msg_counter}, run_time={self._run_time}",
        )

    def get_stats(self):
        return self._loggen.get_loggen_stats()

    async def _simulate_log_generation(self):
        logger.debug("Starting log generation simulation")
        input_msg = ""
        for i in range(100):
            input_msg += self._sample_log

        logger.debug(f"Generated {len(input_msg)} bytes of sample logs")
        with open(input_message_file_name, "w") as f:
            if self._msg_counter != 0:
                logger.debug(f"Using msg_counter mode: {self._msg_counter} messages")
                for i in range(int(self._msg_counter / 100)):
                    f.write(input_msg)

                logger.debug(f"Starting loggen with {self._msg_counter} messages to {self._source_driver.options['ip']}:{self._source_driver.options['port']}")
                self._loggen.start(
                    LoggenStartParams(
                        target=self._source_driver.options["ip"],
                        port=self._source_driver.options["port"],
                        inet=True,
                        perf=True,
                        active_connections=1,
                        number=self._msg_counter,
                        read_file=input_message_file_name,
                        dont_parse=True,
                        loop_reading=False,
                    ),
                )
                logger.debug(f"Waiting for {self._msg_counter} messages to be sent")
                await asyncio.to_thread(
                    wait_until_true_custom,
                    lambda: self._loggen.get_sent_message_count() == self._msg_counter,
                    timeout=300,
                )
                logger.debug(f"All {self._msg_counter} messages sent successfully")

            else:
                logger.debug(f"Using time-based mode: {self._run_time} seconds")
                f.write(input_msg)
                logger.debug(f"Starting loggen for {self._run_time}s to {self._source_driver.options['ip']}:{self._source_driver.options['port']}")
                loggen_proc = self._loggen.start(
                    LoggenStartParams(
                        target=self._source_driver.options["ip"],
                        port=self._source_driver.options["port"],
                        inet=True,
                        perf=True,
                        active_connections=1,
                        interval=self._run_time,
                        read_file=input_message_file_name,
                        dont_parse=True,
                        loop_reading=True,
                    ),
                )
                logger.debug(f"Waiting for loggen process to complete (timeout: {self._run_time + 5}s)")
                await asyncio.to_thread(
                    wait_until_true_custom,
                    lambda: loggen_proc.poll() == 0,
                    timeout=self._run_time + 5,
                )
                logger.debug("Loggen process completed")

    @asynccontextmanager
    async def sending(self):
        logger.debug("Entering LoggenWorker.sending() context")
        self._task = asyncio.create_task(self._simulate_log_generation())
        try:
            yield self
        finally:
            logger.debug("Waiting for log generation task to complete")
            await self._task
            logger.debug("Log generation task completed")

    async def wait_until_done(self):
        logger.debug("LoggenWorker waiting for task completion")
        await self._task
        logger.debug("LoggenWorker task done")


class SyslogNgWorker:
    def __init__(self, syslog_ng_ctl, destination_driver):
        self._task = None
        self._syslog_ng_ctl = syslog_ng_ctl
        self._destination_driver = destination_driver
        self.axosyslog_metrics = []
        logger.debug(f"SyslogNgWorker initialized with destination_driver={type(destination_driver).__name__ if destination_driver else 'None'}")

    async def _simulate_metrics_collection(self):
        logger.debug("Starting metrics collection")
        collection_count = 0
        try:
            while True:
                metrics = {}
                prometheus_metrics = self._syslog_ng_ctl.stats_prometheus()["stdout"]
                collection_count += 1
                for line in prometheus_metrics.splitlines():
                    if not line:
                        continue
                    if "{" in line:
                        metric_name = line.split("{")[0].strip()
                    else:
                        metric_name = line.split()[0].strip()

                    if "result=" in line:
                        metric_name += "/" + line.split('result="')[1].split('"')[0]
                    if metric_name not in metrics:
                        metrics.update({metric_name: []})
                    metrics[metric_name].append(line)

                metrics["timestamp"] = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
                self.axosyslog_metrics.append(metrics)

                if collection_count % 10 == 0:
                    logger.debug(f"Collected {collection_count} metric snapshots")

                await asyncio.sleep(1)
        except asyncio.CancelledError:
            logger.debug(f"Metrics collection cancelled after {collection_count} snapshots")
            pass  # Task was cancelled, exit gracefully

    @asynccontextmanager
    async def collecting_metrics(self):
        logger.debug("Entering SyslogNgWorker.collecting_metrics() context")
        self._task = asyncio.create_task(self._simulate_metrics_collection())
        try:
            yield self
        finally:
            logger.debug("Cancelling metrics collection task")
            self._task.cancel()
            try:
                await self._task
            except asyncio.CancelledError:
                pass
            logger.debug("Metrics collection task cancelled")

    async def wait_until_done(self):
        logger.debug("SyslogNgWorker waiting for completion")
        if self._destination_driver is None:
            logger.debug("No destination driver, returning immediately after short delay")
            await asyncio.sleep(0.5)  # Wait before checking again
            return

        check_count = 0
        missing_metrics_count = 0
        max_missing_metrics_attempts = 8

        while True:
            check_count += 1
            if not self.axosyslog_metrics:
                logger.debug(f"No metrics available yet (check #{check_count})")
                await asyncio.sleep(0.1)
                continue

            last_metrics = self.axosyslog_metrics[-1]

            # Check if the required metrics exist
            if "syslogng_input_events_total" not in last_metrics or "syslogng_output_events_total/delivered" not in last_metrics:
                missing_metrics_count += 1
                logger.debug(f"Required metrics not yet available (check #{check_count}, missing_count={missing_metrics_count}/{max_missing_metrics_attempts})")
                logger.debug(f"Current metrics keys: {list(last_metrics.keys())}")

                if missing_metrics_count >= max_missing_metrics_attempts:
                    logger.error(f"Required metrics still not available after {max_missing_metrics_attempts} attempts. Exiting wait_until_done()")
                    return

                await asyncio.sleep(0.1)
                continue

            intput_events_total = last_metrics["syslogng_input_events_total"][0].split()[1]
            output_events_total = last_metrics["syslogng_output_events_total/delivered"][0].split()[1]

            if intput_events_total == output_events_total:
                logger.info(f"Input and output events match: {intput_events_total} (after {check_count} checks)")
                logger.debug("SyslogNgWorker wait completed successfully")
                break

            if check_count % 10 == 0:
                logger.debug(f"Still waiting after {check_count} checks - Input: {intput_events_total}, Output: {output_events_total}")
            logger.info(f"Waiting for input and output events to match. Current values - Input: {intput_events_total}, Output: {output_events_total}")
            await asyncio.sleep(0.5)  # Wait before checking again


async def run_and_measure_performance(loggen_worker, syslog_ng_worker):
    logger.debug("Starting performance measurement")
    start_time = asyncio.get_running_loop().time()

    async with loggen_worker.sending(), syslog_ng_worker.collecting_metrics():
        logger.debug("Both workers started, waiting for loggen to complete")
        await loggen_worker.wait_until_done()
        logger.debug("Loggen completed, waiting for syslog-ng metrics to stabilize")
        await syslog_ng_worker.wait_until_done()
        logger.debug("Both workers completed")

    end_time = asyncio.get_running_loop().time()
    total_time = end_time - start_time
    logger.debug(f"Performance measurement completed in {total_time:.2f} seconds")
    return total_time
