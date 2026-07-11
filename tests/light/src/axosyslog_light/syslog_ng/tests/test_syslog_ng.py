#!/usr/bin/env python
#############################################################################
# Copyright (c) 2026 Axoflow
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
from unittest.mock import ANY
from unittest.mock import Mock
from unittest.mock import patch

from axosyslog_light.syslog_ng.syslog_ng import CONTROL_SOCKET_TIMEOUT
from axosyslog_light.syslog_ng.syslog_ng import SyslogNg


def test_wait_for_control_socket_alive_uses_extended_timeout():
    syslog_ng = object.__new__(SyslogNg)
    syslog_ng._process = Mock(returncode=0)
    syslog_ng._syslog_ng_ctl = Mock()

    with patch("axosyslog_light.syslog_ng.syslog_ng.wait_until_true_custom", return_value=True) as wait_until_true_custom:
        assert syslog_ng._SyslogNg__wait_for_control_socket_alive()

    wait_until_true_custom.assert_called_once_with(ANY, (syslog_ng,), timeout=CONTROL_SOCKET_TIMEOUT)
