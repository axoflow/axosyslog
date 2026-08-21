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
import pytest

MESSAGES_PER_PRIORITY = 9
TEMPLATE = r'"${MSG}\n"'

KERN_ALERT = 1
MAIL_ALERT = 17
DAEMON_ALERT = 25
AUTH_ALERT = 33
SYSLOG_ALERT = 41
LPR_ALERT = 49

KERN_CRIT = 2
KERN_ERR = 3
KERN_WARNING = 4
KERN_NOTICE = 5
KERN_INFO = 6
KERN_DEBUG = 7

DISTINCT_FACILITIES = [SYSLOG_ALERT, KERN_ALERT, MAIL_ALERT]
DISTINCT_LEVELS = [KERN_DEBUG, KERN_INFO, KERN_NOTICE]


def _tag(priority):
    return "pri{}".format(priority)


def _messages(priority):
    return [
        "<{}>2004-09-07T10:43:21+01:00 bzorp prog[12345]: {} {:05d}".format(priority, _tag(priority), sequence_number)
        for sequence_number in range(1, MESSAGES_PER_PRIORITY + 1)
    ]


@pytest.mark.parametrize(
    "filter_expression, priorities, matching_priorities",
    [
        pytest.param("facility(syslog)", DISTINCT_FACILITIES, [SYSLOG_ALERT], id="facility-syslog"),
        pytest.param("facility(kern)", DISTINCT_FACILITIES, [KERN_ALERT], id="facility-kern"),
        pytest.param("facility(mail)", DISTINCT_FACILITIES, [MAIL_ALERT], id="facility-mail"),
        pytest.param(
            "facility(daemon,auth,lpr)",
            [DAEMON_ALERT, AUTH_ALERT, LPR_ALERT, MAIL_ALERT],
            [DAEMON_ALERT, AUTH_ALERT, LPR_ALERT],
            id="facility-list",
        ),
        pytest.param("level(debug)", DISTINCT_LEVELS, [KERN_DEBUG], id="level-debug"),
        pytest.param("level(info)", DISTINCT_LEVELS, [KERN_INFO], id="level-info"),
        pytest.param("level(notice)", DISTINCT_LEVELS, [KERN_NOTICE], id="level-notice"),
        pytest.param(
            "level(warning..crit)",
            [KERN_WARNING, KERN_ERR, KERN_CRIT, KERN_DEBUG],
            [KERN_WARNING, KERN_ERR, KERN_CRIT],
            id="level-range",
        ),
    ],
)
def test_facility_and_level_filters(
    config, syslog_ng, port_allocator, filter_expression, priorities, matching_priorities,
):
    config.update_global_options(ts_format="iso", keep_hostname="yes", chain_hostnames="no")

    source = config.create_network_source(ip="localhost", port=port_allocator(), transport="tcp")
    log_filter = config.create_filter(filter_expression)
    file_destination = config.create_file_destination(file_name="output.log", template=TEMPLATE)
    config.create_logpath(statements=[source, log_filter, file_destination])

    syslog_ng.start(config)
    source.write_logs([message for priority in priorities for message in _messages(priority)])

    lines = file_destination.read_logs(len(matching_priorities) * MESSAGES_PER_PRIORITY)
    assert sorted({line.rsplit(" ", 1)[0] for line in lines}) == sorted(_tag(p) for p in matching_priorities)
