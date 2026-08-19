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
from axosyslog_light.common.file import File

MESSAGES_PER_TAG = 99
TAGS = ("python1", "python2")
PYTHON_OUTPUT_FILE = "python-output.log"

PREAMBLE = r"""
python {
from syslogng import LogDestination


class TestDestination(LogDestination):
    def init(self, options):
        self.path = options["path"]
        return True

    def open(self):
        return True

    def is_opened(self):
        return True

    def close(self):
        pass

    def deinit(self):
        pass

    def send(self, msg):
        record = {key: value.decode() for key, value in msg.items()}
        with open(self.path, "a") as output:
            output.write("{DATE} {HOST} {MSGHDR}{MSG}\n".format(**record))
        return True
};
"""


def _messages(tag):
    return [
        "<7>2004-09-07T10:43:21+01:00 bzorp prog[12345]: {} {:05d}".format(tag, sequence_number)
        for sequence_number in range(1, MESSAGES_PER_TAG + 1)
    ]


def test_python_destination(config, syslog_ng, port_allocator):
    config.update_global_options(ts_format="iso", keep_hostname="yes", chain_hostnames="no")
    config.add_preamble(PREAMBLE)

    source = config.create_network_source(ip="localhost", port=port_allocator(), transport="tcp")
    python_destination = config.create_python_destination(
        **{
            "class": '"TestDestination"',
            "options": config.arrowed_options({"path": '"{}"'.format(PYTHON_OUTPUT_FILE)}),
            "value-pairs": "key('MSG') pair('HOST', 'bzorp') pair('DATE', '$ISODATE') key('MSGHDR')",
        },
    )
    config.create_logpath(statements=[source, python_destination])

    syslog_ng.start(config)
    source.write_logs([message for tag in TAGS for message in _messages(tag)])

    output_file = File(PYTHON_OUTPUT_FILE)
    output_file.wait_for_creation()
    output_file.open("r")
    try:
        lines = [line.rstrip("\n") for line in output_file.wait_for_number_of_lines(len(TAGS) * MESSAGES_PER_TAG)]
    finally:
        output_file.close()

    assert lines == [
        "2004-09-07T10:43:21+01:00 bzorp prog[12345]: {} {:05d}".format(tag, sequence_number)
        for tag in TAGS
        for sequence_number in range(1, MESSAGES_PER_TAG + 1)
    ]
