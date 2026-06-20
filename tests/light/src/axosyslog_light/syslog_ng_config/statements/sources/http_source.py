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
import typing

import requests
from axosyslog_light.driver_io.http.http_io import HttpIO
from axosyslog_light.syslog_ng_config.statements.sources.source_driver import SourceDriver
from axosyslog_light.syslog_ng_ctl.legacy_stats_handler import LegacyStatsHandler
from axosyslog_light.syslog_ng_ctl.prometheus_stats_handler import PrometheusStatsHandler


class HttpSource(SourceDriver):
    def __init__(
        self,
        stats_handler: LegacyStatsHandler,
        prometheus_stats_handler: PrometheusStatsHandler,
        **options,
    ) -> None:
        port = options.get("port")
        path = options.get("path", "/")
        host = options.get("localip", options.get("ip", "localhost"))
        self.io = HttpIO(port=port, path=path, host=host)

        # string options need to be quoted to render valid config
        for key in ("path", "ip", "localip"):
            if options.get(key) is not None:
                options[key] = f'"{options[key]}"'

        self.driver_name = "http"
        super(HttpSource, self).__init__(stats_handler, prometheus_stats_handler, options=options)

    def write_log(self, message: str) -> None:
        self.io.write_message(message)

    def write_logs(self, messages: typing.List[str]) -> None:
        # the http() source splits a request body at newlines, so a list of
        # messages is sent as a single multi-line POST
        self.io.write_message("\n".join(messages) + "\n")

    def post(self, data: typing.Optional[str] = None, path: typing.Optional[str] = None) -> requests.Response:
        return self.io.post(data=data, path=path)

    def get(self, path: typing.Optional[str] = None) -> requests.Response:
        return self.io.get(path=path)
