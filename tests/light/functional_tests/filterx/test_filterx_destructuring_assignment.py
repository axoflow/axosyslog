#!/usr/bin/env python
#############################################################################
# Copyright (c) 2026 Axoflow
# Copyright (c) 2026 Balazs Scheidler <balazs.scheidler@axoflow.com>
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
from axosyslog_light.syslog_ng_config.renderer import render_statement


def create_config(config, filterx_expr):
    file_true = config.create_file_destination(file_name="dest-true.log", template="'$MSG\n'")
    file_false = config.create_file_destination(file_name="dest-false.log", template="'$MSG\n'")

    raw_config = f"""
@version: {config.get_version()}

options {{ stats(level(1)); }};

source genmsg {{
    example-msg-generator(num(1) template("foobar"));
}};

destination dest_true {{
    {render_statement(file_true)};
}};

destination dest_false {{
    {render_statement(file_false)};
}};

log {{
    source(genmsg);
    if {{
        filterx {{ {filterx_expr} }};
        destination(dest_true);
    }} else {{
        destination(dest_false);
    }};
}};
"""
    config.set_raw_config(raw_config)
    return (file_true, file_false)


def test_tuple_unpack_assignment(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
        (a, b) = (1, 2);
        $MSG = string(a) + "," + string(b);
        """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "1,2"


def test_list_unpack_assignment(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
        [a, b] = [3, 4];
        $MSG = string(a) + "," + string(b);
        """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "3,4"


def test_mixed_tuple_and_list_unpack_assignment(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
        [a, b] = (5, 6);
        (c, d) = [7, 8];
        $MSG = string(a) + "," + string(b) + "," + string(c) + "," + string(d);
        """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "5,6,7,8"


def test_nested_unpack_assignment(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
        (a, [b, c]) = (1, [2, 3]);
        $MSG = string(a) + "," + string(b) + "," + string(c);
        """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "1,2,3"


def test_unpack_assignment_into_message_fields(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
        ($first, $second) = ("foo", "bar");
        $MSG = $first + "," + $second;
        """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "foo,bar"


def test_unpack_assignment_element_count_mismatch(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
        (a, b) = (1, 2, 3);
        """,
    )
    syslog_ng.start(config)

    assert "processed" not in file_true.get_stats()
    assert file_false.get_stats()["processed"] == 1


def test_unpack_assignment_rejects_non_sequence_rhs(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
        (a, b) = "xy";
        """,
    )
    syslog_ng.start(config)

    assert "processed" not in file_true.get_stats()
    assert file_false.get_stats()["processed"] == 1
