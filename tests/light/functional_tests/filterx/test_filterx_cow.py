#!/usr/bin/env python
#############################################################################
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


def test_dict_writes_cause_clone(config, syslog_ng):
    (file_true, file_false, _) = create_config(
        config, [
            """
                d = {
                    'foo':'foovalue',
                    'bar':'barvalue',
                };
                d2 = d;
                d2.bar = 'bar-changed';
                $MSG = string(d) + '--' + string(d2);
            """,
        ],
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == """{"foo":"foovalue","bar":"barvalue"}--{"foo":"foovalue","bar":"bar-changed"}"""


def test_dict_unset_causes_clone(config, syslog_ng):
    (file_true, file_false, _) = create_config(
        config, [
            """
                d = {
                    'foo':'foovalue',
                    'bar':'barvalue',
                };
                d2 = d;
                unset(d2.bar);
                $MSG = string(d) + '--' + string(d2);
            """,
        ],
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == """{"foo":"foovalue","bar":"barvalue"}--{"foo":"foovalue"}"""


def test_dict_child_writes_cause_clone(config, syslog_ng):
    (file_true, file_false, _) = create_config(
        config, [
            """
                d = {
                    'foo':'foovalue',
                    'bar':'barvalue',
                    'child': {
                        'child_foo':'foovalue',
                        'child_bar':'barvalue',
                    },
                };
                d2 = d;
                d2.child.child_bar = 'bar-changed';
                $MSG = d.child.child_bar + '--' + d2.child.child_bar;
            """,
        ],
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == ("""barvalue--bar-changed""")


def test_dict_child_of_child_writes_cause_clone(config, syslog_ng):
    (file_true, file_false, _) = create_config(
        config, [
            """
                d = {
                    'foo':'foovalue',
                    'bar':'barvalue',
                    'child': {
                        'child_foo':'foovalue',
                        'child_bar':'barvalue',
                        'child2': {
                            'child2_foo':'foovalue',
                            'child2_bar':'barvalue',
                        },
                    },
                };
                d2 = d;
                d2.child.child2.child2_bar = 'bar-changed';
                $MSG = d.child.child2.child2_bar + '--' + d2.child.child2.child2_bar;
            """,
        ],
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == ("""barvalue--bar-changed""")


def test_dict_child_of_child_of_child_writes_cause_clone(config, syslog_ng):
    (file_true, file_false, _) = create_config(
        config, [
            """
                d = {
                    'foo':'foovalue',
                    'bar':'barvalue',
                    'child': {
                        'child_foo':'foovalue',
                        'child_bar':'barvalue',
                        'child2': {
                            'child2_foo':'foovalue',
                            'child2_bar':'barvalue',
                            'child3': {
                                'child3_foo':'foovalue',
                                'child3_bar':'barvalue',
                            },
                        },
                    },
                };
                d2 = d;
                d2.child.child2.child3.child3_bar = 'bar-changed';
                $MSG = d.child.child2.child3.child3_bar + '--' + d2.child.child2.child3.child3_bar;
            """,
        ],
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == ("""barvalue--bar-changed""")


def test_list_writes_cause_clone(config, syslog_ng):
    (file_true, file_false, _) = create_config(
        config, [
            """
                l = [1,2,3];
                l2 = l;
                l2[] = 4;
                $MSG = string(l) + '--' + string(l2);
            """,
        ],
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == """[1,2,3]--[1,2,3,4]"""


def test_list_child_writes_cause_clone(config, syslog_ng):
    (file_true, file_false, _) = create_config(
        config, [
            """
                l = [1,2,3,[4,5,6]];
                l2 = l;
                l2[3][] = 10;
                $MSG = string(l) + '--' + string(l2);
            """,
        ],
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == """[1,2,3,[4,5,6]]--[1,2,3,[4,5,6,10]]"""


def test_list_child_of_child_writes_cause_clone(config, syslog_ng):
    (file_true, file_false, _) = create_config(
        config, [
            """
                l = [1,2,3,[4,5,6,{'foo':'foovalue','bar':'barvalue'}]];
                l2 = l;
                l2[3][3].baz = 'bazvalue';
                $MSG = string(l) + '--' + string(l2);
            """,
        ],
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == (
        """[1,2,3,[4,5,6,{"foo":"foovalue","bar":"barvalue"}]]--"""
        """[1,2,3,[4,5,6,{"foo":"foovalue","bar":"barvalue","baz":"bazvalue"}]]"""
    )


def test_list_unset_causes_clone(config, syslog_ng):
    (file_true, file_false, _) = create_config(
        config, [
            """
                l = [1,2,3,[4,5,6,{'foo':'foovalue','bar':'barvalue'}]];
                l2 = l;
                unset(l2[3][3]);
                $MSG = string(l) + '--' + string(l2);
            """,
        ],
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == """[1,2,3,[4,5,6,{"foo":"foovalue","bar":"barvalue"}]]--[1,2,3,[4,5,6]]"""
