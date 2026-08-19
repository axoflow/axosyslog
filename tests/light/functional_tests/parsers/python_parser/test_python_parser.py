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
TAGS = ("python_parser1", "python_parser2")
TEMPLATE = r'"${ISODATE} ${HOST} ${MSGHDR}${FOOBAR}\n"'

PREAMBLE = r"""
python {
from syslogng import LogParser


class TestParser(LogParser):
    def parse(self, msg):
        msg["FOOBAR"] = msg["MSG"]
        return True
};
"""


def _messages(tag):
    return [
        "<7>2004-09-07T10:43:21+01:00 bzorp prog[12345]: {} {:05d}".format(tag, sequence_number)
        for sequence_number in range(1, MESSAGES_PER_TAG + 1)
    ]


def test_python_parser(config, syslog_ng, port_allocator):
    config.update_global_options(ts_format="iso", keep_hostname="yes", chain_hostnames="no")
    config.add_preamble(PREAMBLE)

    source = config.create_network_source(ip="localhost", port=port_allocator(), transport="tcp")
    python_parser = config.create_python_parser(**{"class": '"TestParser"'})
    file_destination = config.create_file_destination(file_name="output.log", template=TEMPLATE)
    config.create_logpath(statements=[source, python_parser, file_destination])

    syslog_ng.start(config)
    source.write_logs([message for tag in TAGS for message in _messages(tag)])

    assert file_destination.read_logs(len(TAGS) * MESSAGES_PER_TAG) == [
        "2004-09-07T10:43:21+01:00 bzorp prog[12345]: {} {:05d}".format(tag, sequence_number)
        for tag in TAGS
        for sequence_number in range(1, MESSAGES_PER_TAG + 1)
    ]
