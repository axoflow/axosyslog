#!/usr/bin/env python
#############################################################################
# Copyright (c) 2025 Balazs Scheidler <balazs.scheidler@axoflow.com>
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
import json

from axosyslog_light.syslog_ng_config.renderer import render_statement


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
        template("{msg.replace('"', '\\"')}")
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


def test_type_dict(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
    variable={"foo":"foovalue", "bar": "barvalue", "int":5, "null":null, "double": 3.14, "datetime":datetime('2006-02-11T10:34:56.123+01:00')};
    $MSG = json();
    $MSG.repr = repr(variable);
    $MSG.string = string(variable);
    $MSG.json = format_json(variable);
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    msg = json.loads(file_final.read_log())
    assert msg["repr"] == """{"foo":"foovalue","bar":"barvalue","int":5,"null":null,"double":3.1400000000000001,"datetime":"1139650496.123000"}"""
    assert msg["string"] == msg["repr"]
    assert msg["json"] == """{"foo":"foovalue","bar":"barvalue","int":5,"null":null,"double":3.1400000000000001,"datetime":"1139650496.123000"}"""


def test_type_list(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
    variable={"foo":"foovalue", "bar": "barvalue", "int":5, "null":null, "double": 3.14, "datetime":datetime('2006-02-11T10:34:56.123+01:00')};
    $MSG = json();
    $MSG.repr = repr(variable);
    $MSG.string = string(variable);
    $MSG.json = format_json(variable);
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    msg = json.loads(file_final.read_log())
    assert msg["repr"] == """{"foo":"foovalue","bar":"barvalue","int":5,"null":null,"double":3.1400000000000001,"datetime":"1139650496.123000"}"""
    assert msg["string"] == msg["repr"]
    assert msg["json"] == """{"foo":"foovalue","bar":"barvalue","int":5,"null":null,"double":3.1400000000000001,"datetime":"1139650496.123000"}"""


def test_type_bytes(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
    variable=bytes("test message");
    $MSG = json();
    $MSG.repr = repr(variable);
    $MSG.string = string(variable);
    $MSG.json = format_json(variable);
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    msg = json.loads(file_final.read_log())
    assert msg["repr"] == """74657374206d657373616765"""
    assert msg["string"] == msg["repr"]
    assert msg["json"] == '"dGVzdCBtZXNzYWdl"'


def test_type_protobuf(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
    variable=protobuf(bytes("test message"));
    $MSG = json();
    $MSG.repr = repr(variable);
    $MSG.string = string(variable);
    $MSG.json = format_json(variable);
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    msg = json.loads(file_final.read_log())
    assert msg["repr"] == """74657374206d657373616765"""
    assert msg["string"] == msg["repr"]
    assert msg["json"] == '"dGVzdCBtZXNzYWdl"'


def test_type_int(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
    variable=155;
    $MSG = json();
    $MSG.repr = repr(variable);
    $MSG.string = string(variable);
    $MSG.json = format_json(variable);
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    msg = json.loads(file_final.read_log())
    assert msg["repr"] == '155'
    assert msg["string"] == msg["repr"]
    assert msg["json"] == '155'


def test_type_double(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
    variable=3.14;
    $MSG = json();
    $MSG.repr = repr(variable);
    $MSG.string = string(variable);
    $MSG.json = format_json(variable);
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    msg = json.loads(file_final.read_log())
    assert msg["repr"] == '3.1400000000000001'
    assert msg["string"] == msg["repr"]
    assert msg["json"] == '3.1400000000000001'


def test_type_bool(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
    variable=true;
    $MSG = json();
    $MSG.repr = repr(variable);
    $MSG.string = string(variable);
    $MSG.json = format_json(variable);
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    msg = json.loads(file_final.read_log())
    assert msg["repr"] == 'true'
    assert msg["string"] == msg["repr"]
    assert msg["json"] == 'true'


def test_type_datetime(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
    variable=datetime('2006-02-11T10:34:56+01:00');
    $MSG = json();
    $MSG.repr = repr(variable);
    $MSG.string = string(variable);
    $MSG.json = format_json(variable);
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    msg = json.loads(file_final.read_log())
    assert msg["repr"] == '2006-02-11T10:34:56.000000+01:00'
    assert msg["string"] == '1139650496.000000'
    # FIXME: this could possibly be a number instead of a string
    assert msg["json"] == '"1139650496.000000"'


def test_type_metrics_labels(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
    variable=metrics_labels({"foo": "foovalue", "bar": "barvalue"});
    $MSG = json();
    $MSG.repr = repr(variable);
    $MSG.string = string(variable);
    $MSG.json = format_json(variable);
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    msg = json.loads(file_final.read_log())
    assert msg["repr"] == """{foo="foovalue",bar="barvalue"}"""
    assert msg["string"] == msg["repr"]
    assert msg["json"] == '{"foo":"foovalue","bar":"barvalue"}'
