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
from axosyslog_light.common.blocking import wait_until_false
from axosyslog_light.common.file import File

BLOCKED_MARKER_FILE = "blocked"

PREAMBLE = r"""
python {
import time
from syslogng import LogDestination


class BlockingDestination(LogDestination):
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
        open(self.path, "w").close()
        while True:
            time.sleep(1)
};
"""


def test_stop_force_terminates_a_stuck_shutdown(config, syslog_ng, syslog_ng_ctl):
    config.add_preamble(PREAMBLE)
    source = config.create_example_msg_generator_source(num=1)
    destination = config.create_python_destination(
        **{
            "class": '"BlockingDestination"',
            "options": config.arrowed_options({"path": '"{}"'.format(BLOCKED_MARKER_FILE)}),
        },
    )
    config.create_logpath(statements=[source, destination])

    syslog_ng.start(config)
    File(BLOCKED_MARKER_FILE).wait_for_creation()

    assert syslog_ng_ctl.stop()["exit_code"] == 0
    assert syslog_ng.wait_for_message_in_console_log("syslog-ng shutting down")
    assert syslog_ng.is_process_running()

    assert syslog_ng_ctl.stop(force=True)["exit_code"] == 0
    assert wait_until_false(syslog_ng.is_process_running)
