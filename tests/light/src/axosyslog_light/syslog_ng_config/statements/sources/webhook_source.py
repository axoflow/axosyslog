#!/usr/bin/env python
#############################################################################
# Copyright (c) 2025 Axoflow
# Copyright (c) 2025 Attila Szakacs <attila.szakacs@axoflow.com>
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as published
# by the Free Software Foundation, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
#
# As an additional exemption you are allowed to compile & link against the
# OpenSSL libraries as published by the OpenSSL project. See the file
# COPYING for details.
#
#############################################################################
import typing

from axosyslog_light.driver_io.http.http_io import HttpIO
from axosyslog_light.syslog_ng_config.statements.sources.source_driver import SourceDriver
from axosyslog_light.syslog_ng_ctl.legacy_stats_handler import LegacyStatsHandler
from axosyslog_light.syslog_ng_ctl.prometheus_stats_handler import PrometheusStatsHandler


class WebhookSource(SourceDriver):
    def __init__(
        self,
        stats_handler: LegacyStatsHandler,
        prometheus_stats_handler: PrometheusStatsHandler,
        **options,
    ) -> None:
        self.io = HttpIO(port=options.get("port"), use_tls="tls_key_file" in options)

        self.driver_name = "webhook"
        super(WebhookSource, self).__init__(stats_handler, prometheus_stats_handler, options=options)

    def write_log(self, message: str) -> None:
        self.io.write_message(message)

    def write_logs(self, messages: typing.List[str]) -> None:
        for message in messages:
            self.io.write_message(message)


class WebhookJsonSource(SourceDriver):
    def __init__(
        self,
        stats_handler: LegacyStatsHandler,
        prometheus_stats_handler: PrometheusStatsHandler,
        **options,
    ) -> None:
        self.io = HttpIO(port=options.get("port"), use_tls="tls_key_file" in options)

        self.driver_name = "webhook-json"
        super(WebhookJsonSource, self).__init__(stats_handler, prometheus_stats_handler, options=options)

    def write_log(self, message: str) -> None:
        self.io.write_json_message(message)

    def write_logs(self, messages: typing.List[str]) -> None:
        for message in messages:
            self.io.write_json_message(message)
