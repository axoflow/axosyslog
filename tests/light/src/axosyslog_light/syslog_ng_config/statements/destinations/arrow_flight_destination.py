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

from axosyslog_light.driver_io.arrow_flight.arrow_flight_io import ArrowFlightIO
from axosyslog_light.syslog_ng_config.statements.destinations.destination_driver import DestinationDriver
from axosyslog_light.syslog_ng_ctl.legacy_stats_handler import LegacyStatsHandler
from axosyslog_light.syslog_ng_ctl.prometheus_stats_handler import PrometheusStatsHandler


class ArrowFlightDestination(DestinationDriver):
    def __init__(
        self,
        stats_handler: LegacyStatsHandler,
        prometheus_stats_handler: PrometheusStatsHandler,
        port: typing.Optional[int] = None,
        **options,
    ) -> None:
        self.driver_name = "arrow-flight"
        self.__port = port
        if port is not None:
            options.setdefault("url", f"'grpc://localhost:{port}'")
        options.setdefault("time_reopen", 1)
        self.options = options
        self._io = None
        super(ArrowFlightDestination, self).__init__(stats_handler, prometheus_stats_handler, None, options)

    def start_listener(self) -> None:
        if self.__port is None:
            raise RuntimeError("Cannot start listener without a port")
        if self._io is None:
            self._io = ArrowFlightIO(self.__port)
        self._io.start_listener()

    def stop_listener(self) -> None:
        if self._io is not None:
            self._io.stop_listener()

    def read_logs(self, path: str) -> list:
        return self._io.read_logs(path)

    def read_log(self, path: str) -> dict:
        return self._io.read_log(path)

    def get_row_count(self, path: str) -> int:
        return self._io.get_row_count(path)

    def get_stream_count(self, path: str) -> int:
        return self._io.get_stream_count(path)

    def get_batch_count(self, path: str) -> int:
        return self._io.get_batch_count(path)
