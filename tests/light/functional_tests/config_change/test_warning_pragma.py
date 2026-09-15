#!/usr/bin/env python
#############################################################################
# Copyright (c) 2026 Axoflow
# Copyright (c) 2026 László Várady <laszlo.varady@axoflow.com>
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

RAW_CONFIG = r"""
block source warned() {
@warning "The warned() source is deprecated"
  example-msg-generator(num(1));
};

source s_warned_1 { warned(); };
source s_warned_2 { warned(); };
"""


def test_warning_pragma_logs_without_stopping_startup(config, syslog_ng):
    config.set_raw_config("@version: {}\n".format(config.get_version()) + RAW_CONFIG)

    syslog_ng.start(config)

    config_path = syslog_ng.instance_paths.get_config_path()
    assert syslog_ng.is_message_in_console_log(f"The warned() source is deprecated; location='{config_path}:8:21'")
    assert syslog_ng.is_message_in_console_log(f"The warned() source is deprecated; location='{config_path}:9:21'")
