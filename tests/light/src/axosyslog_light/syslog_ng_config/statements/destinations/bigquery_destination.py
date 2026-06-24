#!/usr/bin/env python
#############################################################################
# Copyright (c) 2026 Axoflow
# Copyright (c) 2026 Attila Szakacs <attila.szakacs@axoflow.com>
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
import typing

from axosyslog_light.driver_io.bigquery.bigquery_io import BigQueryIO
from axosyslog_light.syslog_ng_config.statements.destinations.destination_driver import DestinationDriver
from axosyslog_light.syslog_ng_ctl.legacy_stats_handler import LegacyStatsHandler
from axosyslog_light.syslog_ng_ctl.prometheus_stats_handler import PrometheusStatsHandler


class BigQueryDestination(DestinationDriver):
    def __init__(
        self,
        stats_handler: LegacyStatsHandler,
        prometheus_stats_handler: PrometheusStatsHandler,
        port: typing.Optional[int] = None,
        project: str = "test-project",
        dataset: str = "test-dataset",
        table: str = "test-table",
        **options,
    ) -> None:
        self.driver_name = "bigquery"
        self.__port = port
        self.__project = project
        self.__dataset = dataset
        self.__table = table
        options.setdefault("project", f"'{project}'")
        options.setdefault("dataset", f"'{dataset}'")
        options.setdefault("table", f"'{table}'")
        if port is not None:
            options.setdefault("url", f"'localhost:{port}'")
            # Talk plaintext to the mock; the real driver defaults to ADC + TLS.
            options.setdefault("auth", "insecure()")
        options.setdefault("time_reopen", 1)
        self.options = options
        self._io = None
        super(BigQueryDestination, self).__init__(stats_handler, prometheus_stats_handler, None, options)

    @property
    def table_path(self) -> str:
        return f"projects/{self.__project}/datasets/{self.__dataset}/tables/{self.__table}"

    def start_listener(self) -> None:
        if self.__port is None:
            raise RuntimeError("Cannot start listener without a port")
        if self._io is None:
            self._io = BigQueryIO(self.__port)
        self._io.start_listener()

    def stop_listener(self) -> None:
        if self._io is not None:
            self._io.stop_listener()

    def restart_stream_after_next_batch(self) -> None:
        if self._io is not None:
            self._io.restart_stream_after_next_batch()

    def respond_with_error(self, code, message) -> None:
        if self._io is not None:
            self._io.respond_with_error(code, message)

    def stop_responding_with_error(self) -> None:
        if self._io is not None:
            self._io.stop_responding_with_error()

    def read_logs(self) -> list:
        return self._io.read_logs(self.table_path)

    def read_log(self) -> dict:
        return self._io.read_log(self.table_path)

    def get_row_count(self) -> int:
        return self._io.get_row_count(self.table_path)

    def get_batch_count(self) -> int:
        return self._io.get_batch_count(self.table_path)

    def get_stream_count(self) -> int:
        return self._io.get_stream_count(self.table_path)

    def get_create_count(self) -> int:
        return self._io.get_create_count(self.table_path)

    def get_finalize_count(self) -> int:
        return self._io.get_finalize_count(self.table_path)
