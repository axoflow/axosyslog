#!/usr/bin/env python
#############################################################################
# Copyright (c) 2025 Balazs Scheidler <balazs.scheidler@axoflow.com>
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


def render_filterx_exprs(expr):
    return f"filterx {{ {expr} }};"


def create_config(config, expr, msg="foobar", template="'$MSG\n'"):
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
            "values.true_string" => string("boolean:true"),
            "values.false_string" => string("boolean:false"),
        )
    );
}};

destination dest_final {{
    {render_statement(file_final)};
}};

log {{
    source(genmsg);
    {render_filterx_exprs(expr)};
    destination(dest_final);
}};
"""
    config.set_raw_config(preamble)
    return (file_final,)


def test_upper(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
    variable="foobar";
    $HOST="hostname";
    $MSG = json();
    $MSG.literal = upper('abc');
    $MSG.variable = upper(variable);
    $MSG.host = upper($HOST);
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == """{"literal":"ABC","variable":"FOOBAR","host":"HOSTNAME"}"""


def test_lower(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
    variable="FoOBaR";
    $HOST="HoStNamE";
    $MSG = json();
    $MSG.literal = lower('AbC');
    $MSG.variable = lower(variable);
    $MSG.host = lower($HOST);
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == """{"literal":"abc","variable":"foobar","host":"hostname"}"""


def test_repr(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
    dt=strptime("2000-01-01T00:00:00Z", "%Y-%m-%dT%H:%M:%S%z");
    $MSG = json();
    $MSG.repr = repr(dt);
    $MSG.str = string(dt);
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == """{"repr":"2000-01-01T00:00:00.000+00:00","str":"946684800.000000"}"""


def test_startswith_with_various_arguments(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
    result = json();
    foo = "foo";
    bar = "bar";
    # literal string
    if (startswith($MSG, "foo"))
    {
        result.startswith_foo1 = true;
    };
    # literal array
    if (startswith($MSG, ["foo"]))
    {
        result.startswith_foo2 = true;
    };
    # literal array, not the first element
    if (startswith($MSG, ["bar", "foo"]))
    {
        result.startswith_foo3 = true;
    };
    # non-literal
    if (startswith($MSG, foo))
    {
        result.startswith_foo4 = true;
    };
    # non-literal
    if (startswith($MSG, [bar, foo]))
    {
        result.startswith_foo4 = true;
    };
    $MSG = result;
    """, msg="fooBARbAz",
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == '{"startswith_foo1":true,"startswith_foo2":true,"startswith_foo3":true,"startswith_foo4":true}'


def test_endswith_with_various_arguments(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
    result = json();
    foo = "foo";
    bar = "bar";
    # literal string
    if (endswith($MSG, "foo"))
    {
        result.endswith_foo1 = true;
    };
    # literal array
    if (endswith($MSG, ["foo"]))
    {
        result.endswith_foo2 = true;
    };
    # literal array, not the first element
    if (endswith($MSG, ["bar", "foo"]))
    {
        result.endswith_foo3 = true;
    };
    # non-literal
    if (endswith($MSG, foo))
    {
        result.endswith_foo4 = true;
    };
    # non-literal
    if (endswith($MSG, [bar, foo]))
    {
        result.endswith_foo4 = true;
    };
    $MSG = result;
    """, msg="bAzBARfoo",
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == '{"endswith_foo1":true,"endswith_foo2":true,"endswith_foo3":true,"endswith_foo4":true}'


def test_includes_with_various_arguments(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
    result = json();
    foo = "foo";
    bar = "bar";
    # literal string
    if (includes($MSG, "foo"))
    {
        result.includes_foo1 = true;
    };
    # literal array
    if (includes($MSG, ["foo"]))
    {
        result.includes_foo2 = true;
    };
    # literal array, not the first element
    if (includes($MSG, ["bar", "foo"]))
    {
        result.includes_foo3 = true;
    };
    # non-literal
    if (includes($MSG, foo))
    {
        result.includes_foo4 = true;
    };
    # non-literal
    if (includes($MSG, [bar, foo]))
    {
        result.includes_foo4 = true;
    };
    $MSG = result;
    """, msg="bAzBARfoo",
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == '{"includes_foo1":true,"includes_foo2":true,"includes_foo3":true,"includes_foo4":true}'


def test_startswith_endswith_includes(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
    result = json();
    if (startswith($MSG, ["dummy_prefix", "foo"]))
    {
        result.startswith_foo = true;
    };
    bar_var = "bar";
    if (includes($MSG, bar_var, ignorecase=true))
    {
        result.contains_bar = true;
    };
    baz_var = "baz";
    baz_list = ["dummy_suffix", baz_var];
    if (endswith($MSG, baz_list, ignorecase=true))
    {
        result.endswith_baz = true;
    };
    log_msg_value = ${values.str};
    if (includes(log_msg_value, log_msg_value))
    {
        result.works_with_message_value = true;
    };
    $MSG = json();
    $MSG = result;
    """, msg="fooBARbAz",
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == '{"startswith_foo":true,"contains_bar":true,"endswith_baz":true,"works_with_message_value":true}'
