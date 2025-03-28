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

from axosyslog_light.driver_io.opentelemetry.opentelemetry_io import OpenTelemetryIO
from axosyslog_light.driver_io.opentelemetry.opentelemetry_io import OTelLog
from axosyslog_light.driver_io.opentelemetry.opentelemetry_io import OTelResource
from axosyslog_light.driver_io.opentelemetry.opentelemetry_io import OTelResourceScopeLog
from axosyslog_light.driver_io.opentelemetry.opentelemetry_io import OTelScope
from axosyslog_light.syslog_ng_config.statements.sources.source_driver import SourceDriver
from axosyslog_light.syslog_ng_ctl.legacy_stats_handler import LegacyStatsHandler
from axosyslog_light.syslog_ng_ctl.prometheus_stats_handler import PrometheusStatsHandler


class OpenTelemetrySource(SourceDriver):
    def __init__(
        self,
        stats_handler: LegacyStatsHandler,
        prometheus_stats_handler: PrometheusStatsHandler,
        **options,
    ) -> None:
        self.io = OpenTelemetryIO(options.get("port", 4317))

        self.driver_name = "opentelemetry"
        super(OpenTelemetrySource, self).__init__(stats_handler, prometheus_stats_handler, options=options)

    def write_log(
        self,
        resource: typing.Optional[OTelResource] = None,
        scope: typing.Optional[OTelScope] = None,
        log: typing.Optional[OTelLog] = None,
    ) -> None:
        self.io.send_logs([OTelResourceScopeLog(resource, scope, log)])

    def write_logs(self, resource_scope_logs: list[OTelResourceScopeLog]) -> None:
        self.io.send_logs(resource_scope_logs)
