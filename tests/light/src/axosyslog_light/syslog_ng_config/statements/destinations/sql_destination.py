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
import sqlite3
import typing
from pathlib import Path

from axosyslog_light.syslog_ng_config import stringify
from axosyslog_light.syslog_ng_config.statements.destinations.destination_driver import DestinationDriver
from axosyslog_light.syslog_ng_ctl.legacy_stats_handler import LegacyStatsHandler
from axosyslog_light.syslog_ng_ctl.prometheus_stats_handler import PrometheusStatsHandler


class SqlDestination(DestinationDriver):
    """sql() destination against a local SQLite file, read back through Python's sqlite3."""

    def __init__(
        self,
        stats_handler: LegacyStatsHandler,
        prometheus_stats_handler: PrometheusStatsHandler,
        database,
        table: str,
        columns: typing.List[str],
        values: typing.List[str],
        **options,
    ) -> None:
        self.database = Path(database).absolute()
        self.table = table

        options = dict(
            {
                "type": "sqlite3",
                "database": stringify(str(self.database)),
                "table": stringify(table),
                "columns": ", ".join(columns),
                "values": ", ".join(values),
            },
            **options,
        )

        self.driver_name = "sql"
        super(SqlDestination, self).__init__(stats_handler, prometheus_stats_handler, options=options)

    def read_rows(self) -> typing.List[tuple]:
        connection = sqlite3.connect(str(self.database))
        try:
            return connection.execute("SELECT * FROM {} ORDER BY rowid".format(self.table)).fetchall()
        finally:
            connection.close()
