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
import typing
from pathlib import Path

from axosyslog_light.driver_io.file.file_io import FileIO
from axosyslog_light.syslog_ng_config import stringify
from axosyslog_light.syslog_ng_config.statements.sources.source_driver import SourceDriver
from axosyslog_light.syslog_ng_ctl.legacy_stats_handler import LegacyStatsHandler
from axosyslog_light.syslog_ng_ctl.prometheus_stats_handler import PrometheusStatsHandler


class WildcardFileSource(SourceDriver):
    def __init__(
        self,
        stats_handler: LegacyStatsHandler,
        prometheus_stats_handler: PrometheusStatsHandler,
        base_dir,
        filename_pattern,
        **options,
    ) -> None:
        self.base_dir = Path(base_dir)
        self.__file_ios = {}

        options = dict(
            {"base-dir": stringify(str(base_dir)), "filename-pattern": stringify(filename_pattern)},
            **options,
        )

        self.driver_name = "wildcard-file"
        super(WildcardFileSource, self).__init__(stats_handler, prometheus_stats_handler, options=options)

    def __get_file_io(self, relative_path) -> FileIO:
        if relative_path not in self.__file_ios:
            path = self.base_dir / relative_path
            path.parent.mkdir(parents=True, exist_ok=True)
            self.__file_ios[relative_path] = FileIO(path)
        return self.__file_ios[relative_path]

    def write_log(self, relative_path, message: str) -> None:
        self.__get_file_io(relative_path).write_message(message)

    def write_logs(self, relative_path, messages: typing.List[str]) -> None:
        self.__get_file_io(relative_path).write_messages(messages)

    def close_files(self) -> None:
        for file_io in self.__file_ios.values():
            file_io.close_writeable_file()
