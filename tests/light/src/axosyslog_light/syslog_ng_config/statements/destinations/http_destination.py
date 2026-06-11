#!/usr/bin/env python
#############################################################################
# Copyright (c) 2026 Axoflow
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
from axosyslog_light.driver_io.http.http_server_io import HttpServerIO
from axosyslog_light.syslog_ng_config.statements.destinations.destination_driver import DestinationDriver
from axosyslog_light.syslog_ng_ctl.legacy_stats_handler import LegacyStatsHandler
from axosyslog_light.syslog_ng_ctl.prometheus_stats_handler import PrometheusStatsHandler


class HttpDestination(DestinationDriver):
    def __init__(
        self,
        stats_handler: LegacyStatsHandler,
        prometheus_stats_handler: PrometheusStatsHandler,
        port: int,
        **options,
    ) -> None:
        self.driver_name = "http"
        self._io = HttpServerIO(
            port,
            response_code=options.pop("response_code", 200),
            responses=options.pop("responses", None),
        )
        self._io.start_listener()

        options.setdefault("url", '"http://127.0.0.1:{}"'.format(port))
        options.setdefault("batch_lines", 1)

        super(HttpDestination, self).__init__(stats_handler, prometheus_stats_handler, [], options)

    def stop_listener(self):
        self._io.stop_listener()

    def read_logs(self, counter):
        return self._io.read_number_of_messages(counter)

    def read_until_logs(self, logs):
        return self._io.read_until_messages(logs)
