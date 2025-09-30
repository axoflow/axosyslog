#!/usr/bin/env python
#############################################################################
# Copyright (c) 2025 Axoflow
# Copyright (c) 2025 Andras Mitzki <andras.mitzki@axoflow.com>
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
from axosyslog_light.driver_io.clickhouse.clickhouse_io import ClickhouseClient
from axosyslog_light.syslog_ng_config.__init__ import destringify
from axosyslog_light.syslog_ng_config.statements.destinations.destination_driver import DestinationDriver
from axosyslog_light.syslog_ng_ctl.legacy_stats_handler import LegacyStatsHandler
from axosyslog_light.syslog_ng_ctl.prometheus_stats_handler import PrometheusStatsHandler


class ClickhouseDestination(DestinationDriver):
    def __init__(
        self,
        stats_handler: LegacyStatsHandler,
        prometheus_stats_handler: PrometheusStatsHandler,
        **options,
    ) -> None:
        self.driver_name = "clickhouse"
        options.setdefault("time_reopen", 1)
        self.options = options
        super(ClickhouseDestination, self).__init__(stats_handler, prometheus_stats_handler, None, options)

    def read_log(self) -> dict:
        return self.read_logs()[0]

    def read_logs(self) -> list[dict]:
        return self.clickhouse_client.select_all_from_table(table_name=self.options["table"])

    def create_table(self, table_name: str, table_columns_and_types: list[tuple[str, str]] = None) -> None:
        self.clickhouse_client.create_table(table_name, table_columns_and_types)

    def delete_table(self) -> None:
        self.clickhouse_client.delete_table()

    def create_clickhouse_client(self, http_port: int) -> None:
        if "user" in self.options and "password" in self.options:
            self.clickhouse_client = ClickhouseClient(username=self.options["user"], password=destringify(self.options["password"]), http_port=http_port)
