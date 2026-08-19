#!/usr/bin/env python
#############################################################################
# Copyright (c) 2026 Axoflow
# Copyright (c) 2026 Attila Szakacs-Bertok <attila.szakacs@axoflow.com>
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
from pathlib import Path

from axosyslog_light.driver_io.pipe.pipe_io import PipeIO
from axosyslog_light.syslog_ng_config.statements.sources.source_driver import SourceDriver
from axosyslog_light.syslog_ng_ctl.legacy_stats_handler import LegacyStatsHandler
from axosyslog_light.syslog_ng_ctl.prometheus_stats_handler import PrometheusStatsHandler


class PipeSource(SourceDriver):
    def __init__(
        self,
        stats_handler: LegacyStatsHandler,
        prometheus_stats_handler: PrometheusStatsHandler,
        file_name,
        **options,
    ) -> None:
        self.path = Path(file_name)
        self.io = PipeIO(self.path, pad_size=int(options.get("pad_size", 0)))

        self.driver_name = "pipe"
        super(PipeSource, self).__init__(stats_handler, prometheus_stats_handler, [self.path], options=options)

    def write_log(self, message, terminator="\n"):
        self.io.write_messages([message], terminator=terminator)

    def write_logs(self, messages, terminator="\n"):
        self.io.write_messages(messages, terminator=terminator)

    def close_file(self):
        self.io.close_file()
