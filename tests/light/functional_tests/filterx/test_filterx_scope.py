#!/usr/bin/env python
#############################################################################
# Copyright (c) 2023 Balazs Scheidler <balazs.scheidler@axoflow.com>
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


# noqa: E122


def render_filterx_exprs(expressions):
    return '\n'.join(f"filterx {{ {expr} }};" for expr in expressions)


def render_log_exprs(expressions):
    return '\n'.join(expressions)


def create_config(config, init_exprs, init_log_exprs=(), true_exprs=(), false_exprs=(), final_log_exprs=(), final_exprs=(), msg="foobar", template="'$MSG\n'"):
    file_true = config.create_file_destination(file_name="dest-true.log", template=template)
    file_false = config.create_file_destination(file_name="dest-false.log", template=template)
    file_final = config.create_file_destination(file_name="dest-final.log", template=template)

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
            "values.json2" => json('{{"foo":{{"foo1":"foo1value","foo2":"foo2value"}},"bar":{{"bar1":"bar1value","bar2":"bar2value"}}}}'),
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

destination dest_final {{
    {render_statement(file_final)};
}};

log {{
    source(genmsg);
    {render_filterx_exprs(init_exprs)};
    {render_log_exprs(init_log_exprs)}
    if {{
        {render_filterx_exprs(true_exprs)}
        destination(dest_true);
    }} else {{
        {render_filterx_exprs(false_exprs)}
        destination(dest_false);
    }};
    {render_log_exprs(final_log_exprs)}
    {render_filterx_exprs(final_exprs)}
    destination(dest_final);
}};
"""
    config.set_raw_config(preamble)
    return (file_true, file_false, file_final)


def test_message_tied_variables_are_propagated_to_the_output(config, syslog_ng):
    (file_true, file_false, _) = create_config(
        config, [
            """
                $foo = "kecske";
                isset($foo);
            """,
            """
                isset($foo);
                $MSG = $foo;
            """,
        ],
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "kecske"


def test_message_tied_variables_in_braces_are_propagated_to_the_output(config, syslog_ng):
    (file_true, file_false, _) = create_config(
        config, [
            """
                ${.foo.bar.baz} = "kecske";
                isset(${.foo.bar.baz});
            """,
            """
                isset(${.foo.bar.baz});
                $MSG = ${.foo.bar.baz};
            """,
        ],
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "kecske"


def test_message_tied_variables_are_propagated_to_the_output_in_junctions(config, syslog_ng):
    (file_true, file_false, _) = create_config(
        config, init_exprs=[
            """
                $foo = "kecske";
                isset($foo);
            """,
        ], true_exprs=[
            """
            isset($foo);
            $MSG = $foo;
            """,
        ],
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "kecske"


def test_message_tied_variables_do_not_propagate_to_parallel_branches(config, syslog_ng):
    (file_true, file_false, _) = create_config(
        config, init_exprs=[
            """
            $foo = "kecske";
            isset($foo);
            """,
        ], true_exprs=[
            """
            isset($foo);
            $bar = $foo;
            isset($bar);
            $foo = "not kecske";
            false;
            """,
        ], false_exprs=[
            """
            isset($foo);
            not isset($bar);
            $MSG = $foo;
            """,
        ],
    )
    syslog_ng.start(config)

    assert file_false.get_stats()["processed"] == 1
    assert "processed" not in file_true.get_stats()
    assert file_false.read_log() == "kecske"


def test_message_tied_variables_are_invalidated_if_message_is_changed(config, syslog_ng):
    (file_true, file_false, file_final) = create_config(
        config, init_exprs=[
            """
                declare foo = $MSG;
                foo;
                $MSG;
                foo == "foobar";
                $MSG == "foobar";
            """,
        ], init_log_exprs=[
            """
                rewrite {
                    set("foobar replacement" value("MSG"));
                };
            """,
        ], true_exprs=[
            """
                $MSG;
                foo;
                $MSG == "foobar replacement";
                foo == "foobar";
            """,
        ],
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "foobar replacement"


def test_message_tied_mutable_objects_are_synced_if_child_object_is_changed(config, syslog_ng):
    (file_true, file_false, file_final) = create_config(
        config, init_exprs=[
            """
                ${values.json2}.foo.foo1 = 'child-changed';
            """,
        ], true_exprs=[
            """
            """,
        ],
        template='''"${values.json2}\n"''',
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == """{"foo":{"foo1":"child-changed","foo2":"foo2value"},"bar":{"bar1":"bar1value","bar2":"bar2value"}}"""


def test_message_tied_variables_are_not_considered_changed_just_by_unmarshaling(config, syslog_ng):
    (file_true, file_false, file_final) = create_config(
        config, init_exprs=[
            """
                # pull up the value from the message into a filterx variable
                ${values.json};
                # cause an unmarshal into JSON
                ${values.json}.emb_key1;
                # $foo is set to the unmarshalled version of ${values.json}
                $foo = ${values.json};
            """,
        ], init_log_exprs=[
            # trigger a sync
            """
                rewrite {
                };
            """,
        ],
        # ${values.json} should retain to have spaces in them, since it was not actually changed, just unmarshalled
        # ${foo} is reformatted from the unmarshalled value
        #
        # NOTE the extra spaces in the assertion below on the $foo part
        template="'${values.json} -- $foo\n'",
    )

    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    (values_json, foo) = file_true.read_log().strip().split(' -- ')
    assert values_json == """{"emb_key1": "emb_key1 value", "emb_key2": "emb_key2 value"}"""
    assert foo == """{"emb_key1":"emb_key1 value","emb_key2":"emb_key2 value"}"""


def test_floating_variables_are_dropped_at_the_end_of_the_scope(config, syslog_ng):
    (file_true, file_false, _) = create_config(
        config, [
            """
            foo = "kecske";
            isset(foo);
            """,
            """
            not isset(foo);
            """,
        ],
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "foobar"


def test_floating_variables_are_dropped_at_the_end_of_the_scope_but_can_be_recreated(config, syslog_ng):
    (file_true, file_false, _) = create_config(
        config, [
            """
            foo = "kecske";
            isset(foo);
            """,
            """
            not isset(foo);
            foo = "barka";
            isset(foo);
            $MSG = foo;
            """,
        ],
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "barka"


def test_declared_variables_are_retained_across_scopes(config, syslog_ng):
    (file_true, file_false, _) = create_config(
        config, [
            """
            declare foo = "kecske";
            isset(foo);
            """,
            """
            isset(foo);
            foo = "barka";
            """,
            """
            isset(foo);
            $MSG = foo;
            """,
        ],
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "barka"


def test_mutable_variables_are_inherited_and_cowed_in_parallel_branches(config, syslog_ng):
    (file_true, file_false, file_final) = create_config(
        config,
        # initialize foo in the prelude, dict in dict
        init_exprs=[
            """
            declare foo = {"foo":{"foo2":"foovalue"}};
            isset(foo);
            """,
        ],
        # in the true branch, inherit the variables from the parent scope and then change "foo"
        # after changing foo, bail out to the false branch
        true_exprs=[
            """
            isset(foo);
            foo.foo.foo2 == "foovalue";
            foo.foo.bar2 = "barvalue";
            """,
            """
            isset(foo);
            foo.foo.foo2 == "foovalue";
            foo.foo.bar2 == "barvalue";
            false;
            """,
        ],
        # the original foo is still intact
        false_exprs=[
            """
            isset(foo);
            foo.foo.foo2 == "foovalue";
            not isset(foo.foo.bar2);
            $MSG = "done";
            """,
        ],
        final_exprs=[
            """
            isset(foo);
            foo.foo.foo2 == "foovalue";
            not isset(foo.foo.bar2);
            $MSG == "done";
            """,
        ],
    )
    syslog_ng.start(config)

    assert file_false.get_stats()["processed"] == 1
    assert "processed" not in file_true.get_stats()
    assert file_false.read_log() == "done"
    assert file_final.read_log() == "done"


def test_declared_variables_are_retained_across_scopes_and_junctions(config, syslog_ng):
    (file_true, file_false, _) = create_config(
        config, init_exprs=[
            """
            declare foo = "kecske";
            isset(foo);
            """,
        ], true_exprs=[
            """
            isset(foo);
            foo = "barka";
            """,
            """
            isset(foo);
            $MSG = foo;
            """,
        ],
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "barka"


def test_changes_in_abandoned_branches_are_ignored(config, syslog_ng):
    (file_true, file_false, file_final) = create_config(
        config, init_exprs=[
            """
                $json = json({"common": "common"});
                $variable = "something";
                unset($variable);
            """,
        ], true_exprs=[
            """
                $json += {"iftrue": "true"};
                $json;
                false;
            """,
        ], false_exprs=[
            """
                $json += {"iffalse": "false"};
                $json;
            """,
        ], final_exprs=[
            """
                $MSG = $json;
            """,
        ],
    )
    syslog_ng.start(config)

    assert file_false.get_stats()["processed"] == 1
    assert "processed" not in file_true.get_stats()
    assert file_false.read_log() == "foobar"

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == '{"common":"common","iffalse":"false"}'
