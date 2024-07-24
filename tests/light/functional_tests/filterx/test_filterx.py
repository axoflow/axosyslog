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
import json

import pytest

from src.syslog_ng_config.renderer import render_statement


def create_config(config, filterx_expr_1, filterx_expr_2=None, msg="foobar"):
    file_true = config.create_file_destination(file_name="dest-true.log", template="'$MSG\n'")
    file_false = config.create_file_destination(file_name="dest-false.log", template="'$MSG\n'")

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


def test_otel_logrecord_int32_setter_getter(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
                                            $olr = otel_logrecord();
                                            $olr.dropped_attributes_count = ${values.int};
                                            $MSG = $olr.dropped_attributes_count; """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "5\n"


def test_otel_logrecord_string_setter_getter(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
                                            $olr = otel_logrecord();
                                            $olr.severity_text = "${values.str}";
                                            $MSG = $olr.severity_text; """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "string\n"


def test_otel_logrecord_bytes_setter_getter(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
                                            $olr = otel_logrecord();
                                            $olr.trace_id = ${values.bytes};
                                            $MSG = $olr.trace_id; """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "binary whatever\n"


def test_otel_logrecord_datetime_setter_getter(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
                                            $olr = otel_logrecord();
                                            $olr.time_unix_nano = ${values.datetime};
                                            $MSG = $olr.time_unix_nano; """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "1701353998.123000+00:00\n"


def test_otel_logrecord_body_string_setter_getter(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
                                            $olr = otel_logrecord();
                                            $olr.body = ${values.str};
                                            $MSG = $olr.body; """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "string\n"


def test_otel_logrecord_body_bool_setter_getter(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
                                            $olr = otel_logrecord();
                                            $olr.body = ${values.bool};
                                            $MSG = $olr.body; """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "true\n"


def test_otel_logrecord_body_int_setter_getter(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
                                            $olr = otel_logrecord();
                                            $olr.body = ${values.int};
                                            $MSG = $olr.body; """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "5\n"


def test_otel_logrecord_body_double_setter_getter(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
                                            $olr = otel_logrecord();
                                            $olr.body = ${values.double};
                                            $MSG = $olr.body; """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "32.5\n"


def test_otel_logrecord_body_datetime_setter_getter(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
                                            $olr = otel_logrecord();
                                            $olr.body = ${values.datetime};
                                            $MSG = $olr.body; """,
    )
    syslog_ng.start(config)

    # there is no implicit conversion back to datetime because anyvalue field
    # does not distinguish int_value and datetime.
    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "1701353998123000\n"


def test_otel_logrecord_body_bytes_setter_getter(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
                                            $olr = otel_logrecord();
                                            $olr.body = ${values.bytes};
                                            $MSG = $olr.body; """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "binary whatever\n"


def test_otel_logrecord_body_protobuf_setter_getter(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
                                            $olr = otel_logrecord();
                                            $olr.body = ${values.protobuf};
                                            $MSG = $olr.body; """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "this is not a valid protobuf!!\n"


def test_otel_logrecord_body_json_setter_getter(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
                                            $olr = otel_logrecord();
                                            $olr.body = ${values.json};
                                            istype($olr.body, "otel_kvlist");
                                            $MSG = format_json($olr.body); """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == '{"emb_key1":"emb_key1 value","emb_key2":"emb_key2 value"}\n'


def test_json_to_otel(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, r"""
                                            js = json({"foo": 42});
                                            js_arr = json_array([1, 2]);

                                            otel_kvl = otel_kvlist();

                                            otel_kvl.js = js;
                                            istype(otel_kvl.js, "otel_kvlist");
                                            otel_kvl.js += {"bar": 1337};

                                            otel_kvl.js_arr = js_arr;
                                            istype(otel_kvl.js_arr, "otel_array");
                                            otel_kvl.js_arr += [3, 4];

                                            $MSG = format_json(otel_kvl);
                """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == '{"js":{"foo":42,"bar":1337},"js_arr":[1,2,3,4]}\n'


def test_otel_to_json(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, r"""
                                            otel_kvl = otel_kvlist({"foo": 42});
                                            otel_arr = otel_array([1, 2]);

                                            js = json();

                                            js.otel_kvl = otel_kvl;
                                            istype(js.otel_kvl, "json_object");
                                            js.otel_kvl += {"bar": 1337};

                                            js.otel_arr = otel_arr;
                                            istype(js.otel_arr, "json_array");
                                            js.otel_arr += [3, 4];

                                            $MSG = js;
                """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == '{"otel_kvl":{"foo":42,"bar":1337},"otel_arr":[1,2,3,4]}\n'


def test_otel_resource_scope_log_to_json(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, r"""
                                            log = otel_logrecord();
                                            log += {
                                                "body": "fit",
                                                "time_unix_nano": 123456789,
                                                "attributes": {
                                                    "answer": 42,
                                                    "cool": true
                                                }
                                            };

                                            resource = otel_resource();
                                            resource.attributes["host.name"] = "szami.to";

                                            scope = otel_scope({
                                                "name": "oszcillo",
                                                "version": "ceta"
                                            });

                                            resource_js = json(resource);
                                            scope_js = json(scope);
                                            log_js = json(log);

                                            js = json({
                                                "in_dict_generator": {
                                                    "resource": resource,
                                                    "scope": scope,
                                                    "log": log,
                                                },
                                                "from_json_cast": {
                                                    "resource": resource_js,
                                                    "scope": scope_js,
                                                    "log": log_js,
                                                }
                                            });

                                            $MSG = js;
                """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert json.loads(file_true.read_log()) == {
        "in_dict_generator": {
            "resource": {
                "attributes": {
                    "host.name": "szami.to",
                },
            },
            "scope": {
                "name": "oszcillo",
                "version": "ceta",
            },
            "log": {
                "time_unix_nano": "123.456789",
                "body": "fit",
                "attributes": {
                    "answer": 42,
                    "cool": True,
                },
            },
        },
        "from_json_cast": {
            "resource": {
                "attributes": {
                    "host.name": "szami.to",
                },
            },
            "scope": {
                "name": "oszcillo",
                "version": "ceta",
            },
            "log": {
                "time_unix_nano": "123.456789",
                "body": "fit",
                "attributes": {
                    "answer": 42,
                    "cool": True,
                },
            },
        },
    }


def test_json_to_otel_resource_scope_log(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, r"""
                                            log_js = json();
                                            log_js += {
                                                "body": "fit",
                                                "time_unix_nano": 123456789,
                                                "attributes": {
                                                    "answer": 42,
                                                    "cool": true
                                                }
                                            };

                                            resource_js = json();
                                            resource_js.attributes = {};
                                            resource_js.attributes["host.name"] = "szami.to";

                                            scope_js = json({
                                                "name": "oszcillo",
                                                "version": "ceta"
                                            });

                                            resource = otel_resource(resource_js);
                                            scope = otel_scope(scope_js);
                                            log = otel_logrecord(log_js);

                                            js = json({
                                                "resource": resource,
                                                "scope": scope,
                                                "log": log,
                                            });

                                            $MSG = js;
                """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert json.loads(file_true.read_log()) == {
        "resource": {
            "attributes": {
                "host.name": "szami.to",
            },
        },
        "scope": {
            "name": "oszcillo",
            "version": "ceta",
        },
        "log": {
            "time_unix_nano": "123.456789",
            "body": "fit",
            "attributes": {
                "answer": 42,
                "cool": True,
            },
        },
    }


def test_simple_true_condition(config, syslog_ng):
    (file_true, file_false) = create_config(config, """ true; """)
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "foobar\n"


def test_empty_block(config, syslog_ng):
    (file_true, file_false) = create_config(config, """ """)
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "foobar\n"


def test_simple_false_condition(config, syslog_ng):
    (file_true, file_false) = create_config(config, """ false; """)
    syslog_ng.start(config)

    assert "processed" not in file_true.get_stats()
    assert file_false.get_stats()["processed"] == 1
    assert file_false.read_log() == "foobar\n"


def test_simple_assignment(config, syslog_ng):

    (file_true, file_false) = create_config(config, """ $MSG = "rewritten message!"; """)
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "rewritten message!\n"


def test_json_assignment_from_template(config, syslog_ng):

    (file_true, file_false) = create_config(config, """ $MSG = "$(format-json --subkeys values.)"; """)
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == """{"true_string":"boolean:true","str":"string","null":null,"list":["foo","bar","baz"],"json":{"emb_key1": "emb_key1 value", "emb_key2": "emb_key2 value"},"int":5,"false_string":"boolean:false","double":32.5,"datetime":"1701350398.123000+01:00","bool":true}\n"""


def test_json_assignment_from_another_name_value_pair(config, syslog_ng):

    (file_true, file_false) = create_config(config, """ $MSG = ${values.json}; """)
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == """{"emb_key1": "emb_key1 value", "emb_key2": "emb_key2 value"}\n"""


def test_json_getattr_returns_the_embedded_json(config, syslog_ng):

    (file_true, file_false) = create_config(
        config, """
$envelope = "$(format-json --subkeys values.)";
$MSG = $envelope.json.emb_key1;
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == """emb_key1 value\n"""


def test_json_setattr_sets_the_embedded_json(config, syslog_ng):

    (file_true, file_false) = create_config(
        config, """
$envelope = "$(format-json --subkeys values.)";
$envelope.json.new_key = "this is a new key!";
$MSG = $envelope.json;
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == """{"emb_key1":"emb_key1 value","emb_key2":"emb_key2 value","new_key":"this is a new key!"}\n"""


def test_json_assign_performs_a_deep_copy(config, syslog_ng):

    (file_true, file_false) = create_config(
        config, """
$envelope = "$(format-json --subkeys values.)";
$envelope.json.new_key = "this is a new key!";
$MSG = $envelope.json;
$envelope.json.another_key = "this is another new key which is not added to $MSG!";
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == """{"emb_key1":"emb_key1 value","emb_key2":"emb_key2 value","new_key":"this is a new key!"}\n"""


def test_json_simple_literal_assignment(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
$MSG = json({
    "foo": "foovalue",
    "bar": "barvalue",
    "baz": "bazvalue"
});
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == """{"foo":"foovalue","bar":"barvalue","baz":"bazvalue"}\n"""


def test_json_recursive_literal_assignment(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
$MSG = json({
    "foo": "foovalue",
    "bar": "barvalue",
    "baz": "bazvalue",
    "recursive": {
        "foo": "foovalue",
        "bar": "barvalue",
        "baz": "bazvalue",
        "recursive": {
            "foo": "foovalue",
            "bar": "barvalue",
            "baz": "bazvalue",
        },
    },
});
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == """{"foo":"foovalue","bar":"barvalue","baz":"bazvalue","recursive":{"foo":"foovalue","bar":"barvalue","baz":"bazvalue","recursive":{"foo":"foovalue","bar":"barvalue","baz":"bazvalue"}}}\n"""


def test_json_change_recursive_literal(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
$MSG = json({
    "foo": "foovalue",
    "bar": "barvalue",
    "baz": "bazvalue",
    "recursive": {
        "foo": "foovalue",
        "bar": "barvalue",
        "baz": "bazvalue",
        "recursive": {
            "foo": "foovalue",
            "bar": "barvalue",
            "baz": "bazvalue",
        },
    },
});

$MSG.recursive.recursive.foo = "changedfoovalue";
$MSG.recursive.recursive.newattr = "newattrvalue";
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == """\
{"foo":"foovalue","bar":"barvalue","baz":"bazvalue",\
"recursive":{"foo":"foovalue","bar":"barvalue","baz":"bazvalue",\
"recursive":{"foo":"changedfoovalue","bar":"barvalue","baz":"bazvalue","newattr":"newattrvalue"}}}\n"""


def test_list_literal_becomes_syslogng_list_as_string(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
$MSG = json_array(["foo", "bar", "baz"]);
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == """foo,bar,baz\n"""


def test_list_literal_becomes_json_list_as_a_part_of_json(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
$list = json_array(["foo", "bar", "baz"]);
$MSG = json({
    "key": "value",
    "list": $list,
});
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == """{"key":"value","list":["foo","bar","baz"]}\n"""


def test_list_is_cloned_upon_assignment(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
$list = json_array(["foo", "bar", "baz"]);
$MSG = $list;
$list[0] = "changed foo";
$MSG[2] = "changed baz";
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    # foo remains unchanged while baz is changed
    assert file_true.read_log() == """foo,bar,"changed baz"\n"""


def test_list_subscript_without_index_appends_an_element(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
$list = json_array();
$list[] = "foo";
$list[] = "bar";
$list[] = "baz";
$MSG = $list;
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    # foo remains unchanged while baz is changed
    assert file_true.read_log() == """foo,bar,baz\n"""


def test_literal_generator_assignment(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
$MSG = json();
$MSG.foo = {"answer": 42, "leet": 1337};
$MSG["bar"] = {"answer+1": 43, "leet+1": 1338};
$MSG.list = ["will be replaced"];
$MSG.list[0] = [1, 2, 3];
$MSG.list[] = [4, 5, 6];
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == """{"foo":{"answer":42,"leet":1337},"bar":{"answer+1":43,"leet+1":1338},"list":[[1,2,3],[4,5,6]]}\n"""


def test_literal_generator_casted_assignment(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
$MSG = json();
$MSG.foo = json({"answer": 42, "leet": 1337});
$MSG["bar"] = json({"answer+1": 43, "leet+1": 1338});
$MSG.list = json_array(["will be replaced"]);
$MSG.list[0] = json_array([1, 2, 3]);
$MSG.list[] = json_array([4, 5, 6]);
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == """{"foo":{"answer":42,"leet":1337},"bar":{"answer+1":43,"leet+1":1338},"list":[[1,2,3],[4,5,6]]}\n"""


def test_function_call(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
$list = json_array();
$list[] = "foo";
$list[] = "bar";
$list[] = "baz";
$MSG = example_echo($list);
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    # foo remains unchanged while baz is changed
    assert file_true.read_log() == """foo,bar,baz\n"""


def test_ternary_operator_true(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    $MSG = true?${values.true_string}:${values.false_string};
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "boolean:true\n"


def test_ternary_operator_false(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    $MSG = false?${values.true_string}:${values.false_string};
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "boolean:false\n"


def test_ternary_operator_expression_true(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    $MSG = (0 === 0)?${values.true_string}:${values.false_string};
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "boolean:true\n"


def test_ternary_operator_expression_false(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    $MSG = (0 === 1)?${values.true_string}:${values.false_string};
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "boolean:false\n"


def test_ternary_operator_inline_ternary_expression_true(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    $MSG = (0 === 0)?("foo" eq "foo"? ${values.true_string} : "inner:false"):${values.false_string};
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "boolean:true\n"


def test_ternary_operator_inline_ternary_expression_false(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    $MSG = (0 === 0)?("foo" eq "bar"? ${values.true_string} : "inner:false"):${values.false_string};
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "inner:false\n"


def test_ternary_return_condition_expression_value_without_true_branch(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    $MSG = ${values.true_string}?:${values.false_string};
""",
    )

    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "boolean:true\n"


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


def test_isset_existing_value_not_in_scope_yet(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    isset($MSG);
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()


def test_isset_existing_value_already_in_scope(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    $MSG;
    isset($MSG);
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()


def test_isset_inexisting_value_not_in_scope_yet(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    not isset($almafa);
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()


def test_isset_inexisting_value_already_in_scope(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    $almafa = "x";
    unset($almafa);
    not isset($almafa);
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()


def test_isset(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    $MSG = json();
    $MSG.inner_key = "foo";

    isset(${values.int});
    not isset($almafa);
    isset($MSG["inner_key"]);
    isset($MSG.inner_key);
    not isset($MSG["almafa"]);
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()


def test_unset_value_not_in_scope_yet(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    unset($MSG);
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "\n"


def test_unset_value_already_in_scope(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    $MSG;
    unset($MSG);
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "\n"


def test_unset_existing_key(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    $MSG = json();
    $MSG["foo"] = "bar";
    unset($MSG["foo"]);
    not isset($MSG["foo"]);
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "{}\n"


def test_unset_inexisting_key(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    $MSG = json();
    unset($MSG["foo"]);
    not isset($MSG["foo"]);
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "{}\n"


def test_setting_an_unset_key_will_contain_the_right_value(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    $MSG = json();
    $MSG["foo"] = "first";
    unset($MSG["foo"]);
    not isset($MSG["foo"]);
    $MSG["foo"] = "second";
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == '{"foo":"second"}\n'


def test_unset(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    $MSG = json();
    $MSG["inner_key"] = "foo";
    $arr = json_array();
    $arr[] = "first";
    $arr[] = "second";

    unset(${values.int}, $almafa, $MSG["inner_key"], $MSG["almafa"], $arr[0]);

    not isset(${values.int});
    not isset($almafa);
    not isset($MSG["inner_key"]);
    not isset($MSG["almafa"]);
    isset($arr[0]);
    not isset($arr[1]);
    unset($MSG);
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "\n"


def test_strptime_error_result(config, syslog_ng):
    _ = create_config(
        config, """
        $MSG = strptime("2024-04-10T08:09:10Z"); # wrong arg set
""",
    )
    with pytest.raises(Exception):
        syslog_ng.start(config)


def test_strptime_success_result(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
        $MSG = strptime("2024-04-10T08:09:10Z", "%Y-%m-%dT%H:%M:%S%z");
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "1712736550.000000+00:00\n"


def test_strptime_failure_result(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
        $MSG = string(strptime("2024-04-10T08:09:10Z", "no", "valid", "parse", "fmt"));
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "null\n"


def test_len(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, r"""
    $dict = json();
    $list = json_array();
    len($dict) == 0;
    len($list) == 0;

    $dict.foo = "bar";
    $list[] = "foo";
    len($dict) == 1;
    len($list) == 1;

    len(${values.str}) == 6;

    $MSG = "success";
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "success\n"


def test_regexp_match(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, r"""
    ${values.str} =~ /string/;
    ${values.str} =~ /.*/;
    not (${values.str} =~ /foobar/);
    "\"" =~ /"/;
    $MSG = "success";
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "success\n"


def test_regexp_match_error_in_pattern(config, syslog_ng):
    _ = create_config(
        config, r"""
    "foo" =~ /(/;
""",
    )
    with pytest.raises(Exception):
        syslog_ng.start(config)


def test_regexp_search(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, r"""
    $MSG = json();
    $MSG.unnamed = regexp_search("foobarbaz", /(foo)(bar)(baz)/);
    $MSG.named = regexp_search("foobarbaz", /(?<first>foo)(?<second>bar)(?<third>baz)/);
    $MSG.mixed = regexp_search("foobarbaz", /(?<first>foo)(bar)(?<third>baz)/);
    $MSG.force_list = json_array(regexp_search("foobarbaz", /(?<first>foo)(bar)(?<third>baz)/));
    $MSG.force_dict = json(regexp_search("foobarbaz", /(foo)(bar)(baz)/));

    $MSG.no_match_unnamed = regexp_search("foobarbaz", /(almafa)/);
    if (len($MSG.no_match_unnamed) == 0) {
        $MSG.no_match_unnamed_handling = true;
    };

    $MSG.no_match_named = regexp_search("foobarbaz", /(?<first>almafa)/);
    if (len($MSG.no_match_named) == 0) {
        $MSG.no_match_named_handling = true;
    };
""",
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert json.loads(file_true.read_log()) == {
        "unnamed": ["foobarbaz", "foo", "bar", "baz"],
        "named": {"0": "foobarbaz", "first": "foo", "second": "bar", "third": "baz"},
        "mixed": {"0": "foobarbaz", "first": "foo", "2": "bar", "third": "baz"},
        "force_list": ["foobarbaz", "foo", "bar", "baz"],
        "force_dict": {"0": "foobarbaz", "1": "foo", "2": "bar", "3": "baz"},
        "no_match_unnamed": [],
        "no_match_unnamed_handling": True,
        "no_match_named": {},
        "no_match_named_handling": True,
    }


def test_regexp_search_error_in_pattern(config, syslog_ng):
    _ = create_config(
        config, r"""
    $MSG = json(regexp_search("foo", /(/));
""",
    )
    with pytest.raises(Exception):
        syslog_ng.start(config)


def test_parse_kv_default_option_set_is_skippable(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    $MSG = parse_kv("foo=bar, thisisstray bar=baz");
    """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == '{"foo":"bar","bar":"baz"}\n'


def test_parse_kv_value_separator(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    $MSG = parse_kv("foo@bar, bar@baz", value_separator="@");
    """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "{\"foo\":\"bar\",\"bar\":\"baz\"}\n"


def test_parse_kv_value_separator_use_first_character(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    $MSG = parse_kv("foo@bar, bar@baz", value_separator="@!$");
    """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "{\"foo\":\"bar\",\"bar\":\"baz\"}\n"


def test_parse_kv_pair_separator(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    $MSG = parse_kv("foo=bar#bar=baz", pair_separator="#");
    """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "{\"foo\":\"bar\",\"bar\":\"baz\"}\n"


def test_parse_kv_stray_words_value_name(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    $MSG = parse_kv("foo=bar, thisisstray bar=baz", stray_words_key="stray_words");
    """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "{\"foo\":\"bar\",\"bar\":\"baz\",\"stray_words\":\"thisisstray\"}\n"


def test_parse_csv_default_arguments(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    custom_message = "foo,bar,baz";
    $MSG = parse_csv(custom_message);
    """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "foo,bar,baz\n"


def test_parse_csv_optional_arg_columns(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    custom_message = "foo,bar,baz";
    cols = json_array(["1st","2nd","3rd"]);
    $MSG = parse_csv(custom_message, columns=cols);
    """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == '{"1st":"foo","2nd":"bar","3rd":"baz"}\n'


def test_parse_csv_optional_arg_delimiters(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    custom_message = "foo bar,baz.tik;tak!toe";
    $MSG = parse_csv(custom_message, delimiter=" ,.");
    """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == 'foo,bar,baz,tik;tak!toe\n'


def test_parse_csv_optional_arg_non_greedy(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    custom_message = "foo,bar,baz,tik,tak,toe";
    cols = json_array(["1st","2nd","3rd"]);
    $MSG = parse_csv(custom_message, columns=cols, greedy=false);
    """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == '{"1st":"foo","2nd":"bar","3rd":"baz"}\n'


def test_parse_csv_optional_arg_greedy(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    custom_message = "foo,bar,baz,tik,tak,toe";
    cols = json_array(["1st","2nd","3rd","rest"]);
    $MSG = parse_csv(custom_message, columns=cols, greedy=true);
    """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == '{"1st":"foo","2nd":"bar","3rd":"baz","rest":"tik,tak,toe"}\n'


def test_parse_csv_optional_arg_strip_whitespace(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, """
    custom_message = " foo   ,  bar  ,  baz, tik,   tak,    toe   ";
    $MSG = parse_csv(custom_message, delimiter=",", strip_whitespaces=true);
    """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == 'foo,bar,baz,tik,tak,toe\n'


def test_parse_csv_dialect(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, r"""
    custom_message = "\"PTHREAD \\\"support initialized\"";
    $MSG = format_json(parse_csv(custom_message, dialect="escape-backslash")); # ["PTHREAD \"support initialized"]
    """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == '["PTHREAD \\"support initialized"]\n'


def test_vars(config, syslog_ng):
    (file_true, file_false) = create_config(
        config,
        filterx_expr_1=r"""
            $logmsg_variable = "foo";
            scope_local_variable = "bar";
            declare pipeline_level_variable = "baz";
        """,
        filterx_expr_2=r"""
            log = otel_logrecord({"body": "foobar", "attributes": {"attribute": 42}});
            js_array = json_array([1, 2, 3, [4, 5, 6]]);
            $MSG = vars();
        """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == '{"logmsg_variable":"foo","pipeline_level_variable":"baz","log":{"body":"foobar","attributes":{"attribute":42}},"js_array":[1,2,3,[4,5,6]]}\n'


def test_unset_empties(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, r"""
            dict = json({"foo": "", "bar": "-", "baz": "N/A", "almafa": null, "kortefa": {"a":{"s":{"d":{}}}}, "szilvafa": [[[]]]});
            defaults_dict = dict;
            explicit_dict = dict;
            unset_empties(defaults_dict);
            unset_empties(explicit_dict, recursive=true);

            list = json_array(["", "-", "N/A", null, {"a":{"s":{"d":{}}}}, [[[]]]]);
            defaults_list = list;
            explicit_list = list;
            unset_empties(defaults_list);
            unset_empties(explicit_list, recursive=true);

            $MSG = json_array([defaults_dict, explicit_dict, defaults_list, explicit_list]);
    """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "[{},{},[],[]]\n"


def test_null_coalesce_use_default_on_null(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, r"""
        x = null;
        y = "bar";
        $MSG = x ?? y;
    """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "bar\n"


def test_null_coalesce_use_default_on_error_and_supress_error(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, r"""
        y = "bar";
        $MSG = len(3.14) ?? y; # in-line expression evaluation is mandatory. the error would be propagated other way
    """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "bar\n"


def test_null_coalesce_get_happy_paths(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, r"""
        data = json({"foo":"1", "bar":"2", "baz":"3"});
        def = "default";
        key = "bar";
        $MSG = json();

        $MSG.a = data[key] ?? def;
        $MSG.b = key ?? def;
    """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == '{"a":"2","b":"bar"}\n'


def test_null_coalesce_get_subscript_error(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, r"""
        data = json({"foo":"1", "bar":"2", "baz":"3"});
        def = "default";
        key = "missing_key";
        $MSG = data[key] ?? def;
    """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "default\n"


def test_null_coalesce_use_nested_coalesce(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, r"""
        data = json({"foo":"1", "bar":"2", "baz":"3"});
        def = "default";
        key1 = "missing_key1";
        key2 = "missing_key2";
        $MSG = data[key1] ?? data[key2] ?? def;
    """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "default\n"


def test_null_coalesce_use_nested_coalesce_return_mid_match(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, r"""
        data = json({"foo":"1", "bar":"2", "baz":"3"});
        def = "default";
        key1 = "missing_key1";
        key2 = "baz";
        $MSG = data[key1] ?? data[key2] ?? def;
    """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == "3\n"


def test_null_coalesce_do_not_supress_last_error(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, r"""
        data = json({"foo":"1", "bar":"2", "baz":"3"});
        def = "default";
        key1 = "missing_key1";
        key2 = "missing_key2";
        key3 = "missing_key3";
        $MSG = data[key1] ?? data[key2] ?? data[key3];
    """,
    )
    syslog_ng.start(config)

    assert file_false.get_stats()["processed"] == 1
    assert "processed" not in file_true.get_stats()


def test_null_coalesce_precedence_versus_ternary(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, r"""
        data = json({"foo":"1", "bar":"2", "baz":"3"});
        def = "default";
        $MSG = json();

        # according to c# and python null coalesce have higher precedence

        # a = ( false ?? isset(data["foo"]) ) ? data["foo"] : def;
        $MSG.a = false ?? isset(data["foo"]) ? data["foo"] : def;

        # b = true ? ( data["wrong"] ?? data["bar"] ) : def;
        $MSG.b = true ? data["wrong"] ?? data["bar"] : def;

        $MSG.c = data["foo"] ? data["bar"] ?? data["baz"] : def;

        # $MSG.d = false ? ( data["foo"] ?? data["bar"] ) : ( data["wrong"] ?? data["baz"] ) ;
        $MSG.d = false ? data["foo"] ?? data["bar"] : data["wrong"] ?? data["baz"];

        # ternary default match value
        $MSG.e = data["wrong"] ?? data["foo"] ? : def;
    """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == '{"a":"default","b":"2","c":"2","d":"3","e":"1"}\n'


def test_slash_string_features(config, syslog_ng):
    cfg = r"""
            $MSG = json();
            $MSG.base = /foo bar/;
            $MSG.line_break = /foo
bar/;
            $MSG.joint_lines = /foo \
bar/;
            ## escaped characters
            $MSG.escaped_backslash = /foo\\bar/;
            $MSG.escaped_slash = /foo\/bar/;
            $MSG.non_escaped_single_quotes = /foo'bar/;
            $MSG.non_escaped_double_quotes = /foo"bar/;
            $MSG.escaped_non_special = /foo\dbar/;

            ## special characters
            $MSG.new_line = /foo\nbar/;
            $MSG.tab = /foo\tbar/;
            $MSG.carrige_return = /foo\rbar/;
            $MSG.vertical_tab = /foo\vbar/;
            $MSG.form_feed = /foo\fbar/;
            $MSG.backspace = /foo\bbar/;
            $MSG.alert = /foo\abar/;

            ##
            $MSG.hexadecimal_value = /foo\x40bar/;
            $MSG.octal_value = /foo\o100bar/;
        """

    (file_true, file_false) = create_config(
        config, cfg,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    exp = (
        r"""{"base":"foo bar","""
        r""""line_break":"foo\nbar","""
        r""""joint_lines":"foo bar","""
        r""""escaped_backslash":"foo\\bar","""
        r""""escaped_slash":"foo\/bar","""
        r""""non_escaped_single_quotes":"foo'bar","""
        r""""non_escaped_double_quotes":"foo\"bar","""
        r""""escaped_non_special":"foo\\dbar","""
        r""""new_line":"foo\nbar","""
        r""""tab":"foo\tbar","""
        r""""carrige_return":"foo\rbar","""
        r""""vertical_tab":"foo\u000bbar","""
        r""""form_feed":"foo\fbar","""
        r""""backspace":"foo\bbar","""
        r""""alert":"foo\u0007bar","""
        r""""hexadecimal_value":"foo@bar","""
        r""""octal_value":"foo@bar"}""" + "\n"
    )
    res = file_true.read_log()
    assert res == exp


def test_regexp_subst(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, r"""
        $MSG = json();
        $MSG.single = regexp_subst("foobarbaz","o","");
        $MSG.empty_string = regexp_subst("","a","!");
        $MSG.empty_pattern = regexp_subst("foobarbaz","","!");
        $MSG.zero_length_match = regexp_subst("foobarbaz","u*","!");
        $MSG.orgrp = regexp_subst("foobarbaz", "(fo|az)", "!");
        $MSG.single_global = regexp_subst("foobarbaz","o","", global=true);
        $MSG.empty_string_global = regexp_subst("","a","!", global=true);
        $MSG.empty_pattern_global = regexp_subst("foobarbaz","","!", global=true);
        $MSG.zero_length_match_global = regexp_subst("foobarbaz","u*","!", global=true);
        $MSG.orgrp_global = regexp_subst("foobarbaz", "(fo|az)", "!", global=true);
        $MSG.ignore_case_control = regexp_subst("FoObArBaz", "(o|a)", "!", global=true);
        $MSG.ignore_case = regexp_subst("FoObArBaz", "(o|a)", "!", ignorecase=true, global=true);
    """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    exp = (
        r"""{"single":"fobarbaz","""
        r""""empty_string":"","""
        r""""empty_pattern":"!foobarbaz!","""
        r""""zero_length_match":"!foobarbaz!","""
        r""""orgrp":"!obarbaz","""
        r""""single_global":"fbarbaz","""
        r""""empty_string_global":"","""
        r""""empty_pattern_global":"!f!o!o!b!a!r!b!a!z!","""
        r""""zero_length_match_global":"!f!o!o!b!a!r!b!a!z!","""
        r""""orgrp_global":"!obarb!","""
        r""""ignore_case_control":"F!ObArB!z","""
        r""""ignore_case":"F!!b!rB!z"}""" + "\n"
    )
    assert file_true.read_log() == exp


def test_regexp_subst_all_args_are_mandatory(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, r"""
        $MSG = regexp_subst("foobarbaz", "(fo|az)");
    """,
    )
    with pytest.raises(Exception):
        syslog_ng.start(config)


def test_add_operator_for_base_types(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, r"""
            $MSG = json();
            $MSG.string = "foo" + "bar" + "baz";
            $MSG.bytes = string(bytes("\xCA") + bytes("\xFE"));
            $MSG.datetime_integer = string(strptime("2000-01-01T00:00:00Z", "%Y-%m-%dT%H:%M:%S%z") + 3600000000);
            $MSG.datetime_double = string(strptime("2000-01-01T00:00:00Z", "%Y-%m-%dT%H:%M:%S%z") + 3600.0);
            $MSG.integer_integer = 3 + 4 + 5;
            $MSG.integer_double = 3 + 0.5;
            $MSG.double_integer = 3.5 + 2;
            $MSG.double_double = 3.14 + 0.86;
            js1 = json_array(["foo","bar"]);
            js2 = json_array(["baz","other"]);
            $MSG.list_list = js1 + js2;
            dict1 = json({"foo":"bar"});
            dict2 = json({"baz":"other"});
            $MSG.dict_dict = dict1 + dict2;
    """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    exp = (
        r"""{"string":"foobarbaz","""
        r""""bytes":"cafe","""
        r""""datetime_integer":"2000-01-01T01:00:00.000+00:00","""
        r""""datetime_double":"2000-01-01T01:00:00.000+00:00","""
        r""""integer_integer":12,"""
        r""""integer_double":3.5,"""
        r""""double_integer":5.5,"""
        r""""double_double":4.0,"""
        r""""list_list":["foo","bar","baz","other"],"""
        r""""dict_dict":{"foo":"bar","baz":"other"}}""" + "\n"
    )
    assert file_true.read_log() == exp


def test_flatten(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, r"""
            dict = json({"top_level_field":42,"top_level_dict":{"inner_field":1337,"inner_dict":{"inner_inner_field":1}}});

            default_separator = dict;
            custom_separator = dict;

            flatten(default_separator);
            flatten(custom_separator, separator="->");

            $MSG = json_array([default_separator, custom_separator]);
    """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    assert file_true.read_log() == '[' \
        '{"top_level_field":42,"top_level_dict.inner_dict.inner_inner_field":1,"top_level_dict.inner_field":1337},' \
        '{"top_level_field":42,"top_level_dict->inner_dict->inner_inner_field":1,"top_level_dict->inner_field":1337}' \
        ']\n'


def test_add_operator_for_generators(config, syslog_ng):
    (file_true, file_false) = create_config(
        config, r"""
            $MSG = json();
            js1 = json_array(["foo","bar"]);
            js2 = json_array(["baz","other"]);
            $MSG.list_var_gen = js1 + ["baz1","other1"];
            $MSG.list_gen_var = ["foo2", "bar2"] + js2;
            $MSG.list_gen_gen = ["foo3", "bar3"] + ["baz3", "other3"];
            dict1 = json({"foo":{"bar":"baz"}});
            dict2 = json({"tik":{"tak":"toe"}});
            $MSG.dict_var_gen = dict1 + {"tik1":{"tak1":"toe1"}};
            $MSG.dict_gen_var = {"foo2":{"bar2":"baz2"}} + dict2;
            $MSG.dict_gen_gen = {"foo3":{"bar3":"baz3"}} + {"tik3":{"tak3":"toe3"}};
    """,
    )
    syslog_ng.start(config)

    assert file_true.get_stats()["processed"] == 1
    assert "processed" not in file_false.get_stats()
    exp = (
        r"""{"list_var_gen":["foo","bar","baz1","other1"],"""
        r""""list_gen_var":["foo2","bar2","baz","other"],"""
        r""""list_gen_gen":["foo3","bar3","baz3","other3"],"""
        r""""dict_var_gen":{"foo":{"bar":"baz"},"tik1":{"tak1":"toe1"}},"""
        r""""dict_gen_var":{"foo2":{"bar2":"baz2"},"tik":{"tak":"toe"}},"""
        r""""dict_gen_gen":{"foo3":{"bar3":"baz3"},"tik3":{"tak3":"toe3"}}}""" + "\n"
    )
    assert file_true.read_log() == exp
