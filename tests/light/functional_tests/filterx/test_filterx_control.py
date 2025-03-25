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
import pytest
from axosyslog_light.syslog_ng_config.renderer import render_statement


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
    assert file_true.read_log() == "matched"


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
    assert file_true.read_log() == "default"


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
    assert file_true.read_log() == "default"


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
    assert file_true.read_log() == "matched"


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
    assert file_true.read_log() == "elif-matched"


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
    assert file_true.read_log() == "else-matched"


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
    assert file_true.read_log() == "matched"


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
    assert file_true.read_log() == "default"


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
    assert file_true.read_log() == 'bar'
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
    assert file_true.read_log() == '{"$MESSAGE":"foo","var_wont_change":true}'


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
    assert file_true.read_log() == '{"$MESSAGE":"foo","var_wont_change":true,"new_variable":true}'


def test_switch_right_case_is_picked_from_the_middle(config, syslog_ng):
    (file_true, file_false) = create_config(
        config,
        filterx_expr_1=r"""
            switch (${values.str}) {
              case "match1":
                result = "does-not-match1";
                break;
              case "string":
                result = "that's right";
                break;

              case "match2":
                result = "does-not-match2";
                break;
              default:
                result = "does-not-match-default";
                break;
            };
            $MSG=result;
        """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "that's right"


def test_switch_fallthrough(config, syslog_ng):
    (file_true, file_false) = create_config(
        config,
        filterx_expr_1=r"""
            switch (${values.str}) {
              case "string":
                result = "that's right";
                # fallthrough
              case "match2":
                result = "fallthrough";
                break;
              default:
                result = "does-not-match-default";
                break;
            };
            $MSG=result;
        """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "fallthrough"


def test_switch_fallthrough_twice(config, syslog_ng):
    (file_true, file_false) = create_config(
        config,
        filterx_expr_1=r"""
            switch (${values.str}) {
              case "string":
                result = "that's right";
                # fallthrough
              case "match2":
                result = "fallthrough";
                # fallthrough
              default:
                result = "fallthrough2";
                break;
            };
            $MSG=result;
        """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "fallthrough2"


def test_switch_default_case(config, syslog_ng):
    (file_true, file_false) = create_config(
        config,
        filterx_expr_1=r"""
            switch (${values.str}) {
              case "match1":
                result = "does not match1";
                break;
              case "match2":
                result = "does not match2";
                break;
              default:
                result = "default-case";
                break;
            };
            $MSG=result;
        """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "default-case"


def test_switch_no_match_case_no_default_case(config, syslog_ng):
    (file_true, file_false) = create_config(
        config,
        filterx_expr_1=r"""
            result = "no-match-no-default";
            switch (${values.str}) {
              case "match1":
                result = "does not match1";
                break;
              case "match2":
                result = "does not match2";
                break;
            };
            $MSG=result;
        """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "no-match-no-default"


def test_switch_variable_in_case(config, syslog_ng):
    (file_true, file_false) = create_config(
        config,
        msg="string",
        filterx_expr_1=r"""
            switch (${values.str}) {
              case $MSG:
                result = "that's right";
                break;
              default:
                result = "default-case";
                break;
            };
            $MSG=result;
        """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "that's right"


# Implicitly testing optimized expression caching
def test_switch_duplicate_literal_case(config, syslog_ng):
    _ = create_config(
        config,
        msg="string",
        filterx_expr_1=r"""
            switch (${values.str}) {
              case "string":
                result = "that's right";
                break;
              case true ? "string" : "something else":
                result = "that wants to be right again, but no bueno";
                break;
            };
            $MSG=result;
        """,
    )
    with pytest.raises(Exception):
        syslog_ng.start(config)


def test_switch_duplicate_default_case(config, syslog_ng):
    _ = create_config(
        config,
        msg="string",
        filterx_expr_1=r"""
            switch (${values.str}) {
              default:
                result = "that's right";
                break;
              default:
                result = "that wants to be right again, but no bueno";
                break;
            };
            $MSG=result;
        """,
    )
    with pytest.raises(Exception):
        syslog_ng.start(config)


def test_switch_invalid_selector(config, syslog_ng):
    (file_true, file_false) = create_config(
        config,
        msg="orig_msg",
        filterx_expr_1=r"""
            switch (invalid.expr) {
              case $MSG:
                result = "no-no";
                break;
            };
            $MSG=result;
        """,
    )
    syslog_ng.start(config)

    assert file_false.get_stats()["processed"] == 1
    assert "processed" not in file_true.get_stats()
    assert file_false.read_log() == "orig_msg"


def test_switch_invalid_case(config, syslog_ng):
    (file_true, file_false) = create_config(
        config,
        msg="string",
        filterx_expr_1=r"""
            switch (${values.str}) {
              case invalid.expr:
                result = "no-no";
                break;
              default:
                result = "default";
                break;
            };
            $MSG=result;
        """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "default"
