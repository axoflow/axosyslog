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
import json as json_module
import typing

import requests
from axosyslog_light.driver_io.http.http_io import HttpIO
from axosyslog_light.syslog_ng_config.statements.sources.source_driver import SourceDriver
from axosyslog_light.syslog_ng_ctl.legacy_stats_handler import LegacyStatsHandler
from axosyslog_light.syslog_ng_ctl.prometheus_stats_handler import PrometheusStatsHandler

HEC_EVENT_PATH = "/services/collector/event"


class SplunkHecSource(SourceDriver):
    def __init__(
        self,
        stats_handler: LegacyStatsHandler,
        prometheus_stats_handler: PrometheusStatsHandler,
        **options,
    ) -> None:
        port = options.get("port")
        host = options.get("localip", "localhost")
        self.io = HttpIO(port=port, path=HEC_EVENT_PATH, host=host)

        # string options need to be quoted to render valid config
        for key in ("localip", "prefix"):
            if options.get(key) is not None:
                options[key] = f'"{options[key]}"'

        self.driver_name = "splunk_hec"
        super(SplunkHecSource, self).__init__(stats_handler, prometheus_stats_handler, options=options)

    def send_event(self, event: typing.Union[str, dict], **envelope) -> requests.Response:
        # build a HEC JSON event object: {"event": ..., <envelope>}
        hec = dict(envelope)
        hec["event"] = event
        return self.io.post(data=json_module.dumps(hec))

    def send_events(self, events: typing.List[typing.Union[str, dict]]) -> requests.Response:
        # multiple HEC events are batched as newline-delimited JSON objects
        body = "\n".join(json_module.dumps({"event": event}) for event in events) + "\n"
        return self.io.post(data=body)

    def post(self, data: typing.Optional[str] = None, path: typing.Optional[str] = None) -> requests.Response:
        return self.io.post(data=data, path=path)
