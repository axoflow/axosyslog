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
MESSAGES_PER_TAG = 99
FLUSH_LINES = 25
TAGS = ("sql1", "sql2")
DEFAULT_COLUMN_VALUE = 5678


def _messages(tag):
    return [
        "<7>2004-09-07T10:43:21+01:00 bzorp prog[12345]: {} {:05d}".format(tag, sequence_number)
        for sequence_number in range(1, MESSAGES_PER_TAG + 1)
    ]


def test_sql_destination(config, syslog_ng, port_allocator):
    config.update_global_options(keep_hostname="yes")

    source = config.create_network_source(ip="localhost", port=port_allocator(), transport="tcp")
    sql_destination = config.create_sql_destination(
        database="test-sql.db",
        table="logs",
        columns=['"date datetime"', '"host"', '"program"', '"pid"', '"msg"', '"dummy int default 5678"'],
        values=['"$DATE"', '"$HOST"', '"$PROGRAM"', '"${PID:-@NULL@}"', '"$MSG"', "default"],
        indexes='"date", "host", "program"',
        null='"@NULL@"',
        flags="explicit-commits",
        flush_lines=FLUSH_LINES,
    )
    # The file destination only tells the test when every message arrived,
    # so that shutdown does not cut the sql() batches short.
    file_destination = config.create_file_destination(file_name="output.log", template=r'"${MSG}\n"')
    config.create_logpath(statements=[source, sql_destination, file_destination])

    syslog_ng.start(config)
    source.write_logs([message for tag in TAGS for message in _messages(tag)])
    file_destination.read_logs(len(TAGS) * MESSAGES_PER_TAG)

    # flags(explicit-commits) only commits the last, partial batch at shutdown
    syslog_ng.stop()

    rows = sql_destination.read_rows()
    assert len(rows) == len(TAGS) * MESSAGES_PER_TAG

    for _, host, program, pid, _, default_column in rows:
        assert (host, program, pid, default_column) == ("bzorp", "prog", "12345", DEFAULT_COLUMN_VALUE)

    assert [msg for _, _, _, _, msg, _ in rows] == [
        "{} {:05d}".format(tag, sequence_number)
        for tag in TAGS
        for sequence_number in range(1, MESSAGES_PER_TAG + 1)
    ]
