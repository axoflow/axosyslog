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

    def get_loggen_stats(self):
        return self._loggen.get_loggen_stats()

    async def _simulate_log_generation(self):
        input_msg = ""
        for i in range(100):
            input_msg += self._sample_log

        with open(input_message_file_name, "w") as f:
            if self._msg_counter != 0:
                for i in range(int(self._msg_counter / 100)):
                    f.write(input_msg)

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
                await asyncio.to_thread(
                    wait_until_true_custom,
                    lambda: self._loggen.get_sent_message_count() == self._msg_counter,
                    timeout=300,
                )

            else:
                f.write(input_msg)
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
                await asyncio.to_thread(
                    wait_until_true_custom,
                    lambda: loggen_proc.poll() == 0,
                    timeout=self._run_time + 5,
                )

    @asynccontextmanager
    async def sending(self):
        self._task = asyncio.create_task(self._simulate_log_generation())
        try:
            yield self
        finally:
            await self._task

    async def wait_until_done(self):
        await self._task


class SyslogNgWorker:
    def __init__(self, syslog_ng_ctl, destination_driver):
        self._task = None
        self._syslog_ng_ctl = syslog_ng_ctl
        self._destination_driver = destination_driver
        self.axosyslog_metrics = []

    async def _simulate_metrics_collection(self):
        try:
            while True:
                metrics = {}
                prometheus_metrics = self._syslog_ng_ctl.stats_prometheus()["stdout"]
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

                await asyncio.sleep(1)
        except asyncio.CancelledError:
            pass  # Task was cancelled, exit gracefully

    @asynccontextmanager
    async def collecting_metrics(self):
        self._task = asyncio.create_task(self._simulate_metrics_collection())
        try:
            yield self
        finally:
            self._task.cancel()
            try:
                await self._task
            except asyncio.CancelledError:
                pass

    async def wait_until_done(self):
        if self._destination_driver is None:
            await asyncio.sleep(0.5)  # Wait before checking again
            return
        while True:
            if not self.axosyslog_metrics:
                await asyncio.sleep(0.1)
                continue

            last_metrics = self.axosyslog_metrics[-1]

            # Check if the required metrics exist
            if "syslogng_input_events_total" not in last_metrics or "syslogng_output_events_total/delivered" not in last_metrics:
                await asyncio.sleep(0.1)
                continue

            intput_events_total = last_metrics["syslogng_input_events_total"][0].split()[1]
            output_events_total = last_metrics["syslogng_output_events_total/delivered"][0].split()[1]

            if intput_events_total == output_events_total:
                logger.info(f"Input and output events match: {intput_events_total}")
                break

            logger.info(f"Waiting for input and output events to match. Current values - Input: {intput_events_total}, Output: {output_events_total}")
            await asyncio.sleep(0.5)  # Wait before checking again


async def run_and_measure_performance(loggen_worker, syslog_ng_worker):
    start_time = asyncio.get_running_loop().time()

    async with loggen_worker.sending(), syslog_ng_worker.collecting_metrics():
        await loggen_worker.wait_until_done()
        await syslog_ng_worker.wait_until_done()

    end_time = asyncio.get_running_loop().time()
    total_time = end_time - start_time
    return total_time
