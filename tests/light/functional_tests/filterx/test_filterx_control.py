#!/usr/bin/env python
#############################################################################
# Copyright (c) 2024 Axoflow
# Copyright (c) 2024 Tamás Kosztyu <tamas.kosztyu@axoflow.com>
# Copyright (c) 2023 Balazs Scheidler <balazs.scheidler@axoflow.com>
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as published
# by the Free Software Foundation, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
#
# As an additional exemption you are allowed to compile & link against the
# OpenSSL libraries as published by the OpenSSL project. See the file
# COPYING for details.
#
#############################################################################
import json
from datetime import datetime
from datetime import timezone

import pytest

from src.syslog_ng_config.renderer import render_statement


def create_config(config, filterx_expr_1, filterx_expr_2=None, msg="foobar", template="'$MSG\n'"):
    file_true = config.create_file_destination(file_name="dest-true.log", template=template)
    file_false = config.create_file_destination(file_name="dest-false.log", template=template)

    preamble = f"""
@version: {config.get_version()}

options {{ stats(level(1)); }};

source genmsg {{
    example-msg-generator(
        num(1)
        template("{msg}")
        values(
            "values.str" => string("string"),
            "values.bool" => boolean(true),
            "values.int" => int(5),
            "values.double" => double(32.5),
            "values.datetime" => datetime("1701350398.123000+01:00"),
            "values.list" => list("foo,bar,baz"),
            "values.null" => null(""),
            "values.bytes" => bytes("binary whatever"),
            "values.protobuf" => protobuf("this is not a valid protobuf!!"),
            "values.json" => json('{{"emb_key1": "emb_key1 value", "emb_key2": "emb_key2 value"}}'),
            "values.true_string" => string("boolean:true"),
            "values.false_string" => string("boolean:false"),
        )
    );
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
        filterx {{ {filterx_expr_1} \n}};
        {"filterx { " + filterx_expr_2 + " };" if filterx_expr_2 is not None else ""}
        destination(dest_true);
    }} else {{
        destination(dest_false);
    }};
}};
"""
    config.set_raw_config(preamble)
    return (file_true, file_false)


def test_if_condition_without_else_branch_match(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    $out = "default";
    if (true) {
        $out = "matched";
    };
    $MSG = $out;
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "matched\n"


def test_if_condition_without_else_branch_nomatch(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    $out = "default";
    if (false) {
        $out = "matched";
    };
    $MSG = $out;
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "default\n"


def test_if_condition_no_matching_condition(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    $out = "default";
    if (false) {
        $out = "matched";
    } elif (false) {
        $out = "elif-matched";
    };
    $MSG = $out;
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "default\n"


def test_if_condition_matching_main_condition(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    $out = "default";
    if (true) {
        $out = "matched";
    } elif (true) {
        $out = "elif-matched";
    } else {
        $out = "else-matched";
    };
    $MSG = $out;
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "matched\n"


def test_if_condition_matching_elif_condition(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    $out = "default";
    if (false) {
        $out = "matched";
    } elif (true) {
        $out = "elif-matched";
    } elif (true) {
        $out = "elif2-matched";
    } else {
        $out = "else-matched";
    };
    $MSG = $out;
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "elif-matched\n"


def test_if_condition_matching_else_condition(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    $out = "default";
    if (false) {
        $out = "matched";
    } elif (false) {
        $out = "elif-matched";
    } else {
        $out = "else-matched";
    };
    $MSG = $out;
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "else-matched\n"


def test_if_condition_matching_expression(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    $out = "default";
    if ("foo" eq "foo") {
        $out = "matched";
    };
    $MSG = $out;
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "matched\n"


def test_if_condition_non_matching_expression(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    $out = "default";
    if ("foo" eq "bar") {
        $out = "matched";
    };
    $MSG = $out;
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "default\n"


def test_drop(config, syslog_ng):
    file_true = config.create_file_destination(file_name="dest-true.log", template="'$MSG\n'")
    file_false = config.create_file_destination(file_name="dest-false.log", template="'$MSG\n'")

    raw_conf = f"""
@version: {config.get_version()}

options {{ stats(level(1)); }};

source genmsg {{
    example-msg-generator(num(1) template("foo"));
    example-msg-generator(num(1) template("bar"));
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
        filterx {{ {"if ($MSG =~ 'foo') {drop;};"} \n}};
        destination(dest_true);
    }} else {{
        destination(dest_false);
    }};
}};
"""
    config.set_raw_config(raw_conf)

    syslog_ng.start(config)

    assert "processed" in file_true.get_stats()
    assert file_true.get_stats()["processed"] == 1
    assert file_true.read_log() == 'bar\n'
    assert syslog_ng.wait_for_message_in_console_log("filterx rule evaluation result; result='explicitly dropped'") != []


def test_done(config, syslog_ng):
    (file_true, file_false) = create_config(
        config,
        msg="foo",
        filterx_expr_1=r"""
            if ($MSG =~ 'foo') {
              declare var_wont_change = true;
              done;
              var_wont_change = false; # This will be skipped
            };
        """,
        filterx_expr_2=r"""
            $MSG = vars();
        """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == '{"$MESSAGE":"foo","var_wont_change":true}\n'


def test_break(config, syslog_ng):
    (file_true, file_false) = create_config(
        config,
        msg="foo",
        filterx_expr_1=r"""
            if ($MSG =~ 'foo') {
              declare var_wont_change = true;
              break;
              var_wont_change = false; # This will be skipped
            };
            declare new_variable = true;
        """,
        filterx_expr_2=r"""
            $MSG = vars();
            new_variable;
        """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == '{"$MESSAGE":"foo","var_wont_change":true,"new_variable":true}\n'
