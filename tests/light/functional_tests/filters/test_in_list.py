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
import pytest


@pytest.mark.parametrize(
    "statement",
    [
        'filter {{ in-list("{list_file}", value("PROGRAM")); }};',
        'rewrite {{ set("x", value("MSG") condition(in-list("{list_file}", value("PROGRAM")))); }};',
    ],
    ids=["filter", "rewrite_condition"],
)
def test_missing_in_list_file_fails_the_startup(config, syslog_ng, tmp_path, statement):
    config.set_raw_config(
        f"""
@version: {config.get_version()}

log {{
    source {{ example-msg-generator(num(1)); }};
    {statement.format(list_file=tmp_path / "missing.list")}
}};
""",
    )

    with pytest.raises(Exception, match="syslog-ng is not running"):
        syslog_ng.start(config)


def test_failed_reload_keeps_running_without_the_in_list_file(config, syslog_ng, syslog_ng_ctl, tmp_path):
    list_file = tmp_path / "test.list"
    list_file.write_text("test-program\n")
    config.set_raw_config(
        f"""
@version: {config.get_version()}

log {{
    source {{ example-msg-generator(num(1)); }};
    filter {{ in-list("{list_file}", value("PROGRAM")); }};
}};
""",
    )
    syslog_ng.start(config)

    list_file.unlink()
    syslog_ng_ctl.reload()

    syslog_ng.wait_for_message_in_console_log("Error initializing new configuration, reverting to old config")
    assert syslog_ng_ctl.is_control_socket_alive()
