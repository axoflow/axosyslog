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
import base64
import json
import math
import shutil
import subprocess
from pathlib import Path

import pytest
from axosyslog_light.common.file import File
from axosyslog_light.syslog_ng_config.renderer import render_statement
from axosyslog_light.syslog_ng_ctl.prometheus_stats_handler import MetricFilter
from google.protobuf import descriptor_pb2
from google.protobuf import message_factory
from google.protobuf.message import Message

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


def test_dict_from_message_variable(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
        $MSG = dict(${values.json});
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == """{"emb_key1":"emb_key1 value","emb_key2":"emb_key2 value"}"""


def test_list_from_message_variable(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
        $MSG = list(${values.list});
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == """foo,bar,baz"""


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
    if (includes($MSG, "bAz", limit=3))
    {
        result.includes_limited3_baz = true;
    };
    if (includes($MSG, "bAz", limit=2))
    {
        result.includes_limited2_baz = true;
    };
    if (includes($MSG, "BAR", limit=6))
    {
        result.includes_limited6_bar = true;
    };
    if (includes($MSG, "foo", limit=6))
    {
        result.includes_limited6_foo = true;
    };
    $MSG = result;
    """, msg="bAzBARfoo",
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == '{"includes_foo1":true,"includes_foo2":true,"includes_foo3":true,"includes_foo4":true,"includes_limited3_baz":true,"includes_limited6_bar":true}'


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


def test_cache_json_file(config, syslog_ng, testcase_parameters):
    (file_final,) = create_config(
        config, r"""
    lookup = cache_json_file("{}/cache_json_file.json");
    $MSG = lookup.foo["foo/foo"];
    """.format(testcase_parameters.get_testcase_dir()), msg="fooBARbAz",
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == 'foo/foo_value'


def test_unset_empties_defaults_dict(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
            dict = {"foo": "", "bar": "-", "baz": "N/A", "almafa": null, "kortefa": {"a":{"s":{"d":{}}}}, "szilvafa": [[[]]]};
            defaults_dict = dict;
            unset_empties(defaults_dict, recursive=false);

            $MSG = defaults_dict;
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == r"""{"bar":"-","baz":"N/A","kortefa":{"a":{"s":{"d":{}}}},"szilvafa":[[[]]]}"""


def test_unset_empties_explicit_dict(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
            dict = {"foo": "", "bar": "-", "baz": "N/A", "almafa": null, "kortefa": {"a":{"s":{"d":{}}}}, "szilvafa": [[[]]]};
            explicit_dict = dict;
            unset_empties(explicit_dict, recursive=true);

            $MSG = explicit_dict;
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == r"""{"bar":"-","baz":"N/A"}"""


def test_unset_empties_no_defaults_dict(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
            dict = {"foo": "", "bar": "-", "baz": "N/A", "almafa": null, "kortefa": {"a":{"s":{"d":{}}}}, "szilvafa": [[[]]]};
            no_defaults_dict = dict;
            unset_empties(no_defaults_dict, targets=["n/a", "-"], recursive=true, ignorecase=true);

            $MSG = no_defaults_dict;
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == r"""{"foo":"","almafa":null,"kortefa":{"a":{"s":{"d":{}}}},"szilvafa":[[[]]]}"""


def test_unset_empties_target_dict(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
            dict = {"foo": "", "bar": "-", "baz": "N/A", "almafa": null, "kortefa": {"a":{"s":{"d":{}}}}, "szilvafa": [[[]]]};
            target_dict = dict;
            unset_empties(target_dict, targets=["n/a", "-", null, "", {}, []], recursive=true, ignorecase=false);

            $MSG = target_dict;
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == r"""{"baz":"N/A"}"""


def test_unset_empties_ignorecase_dict(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
            dict = {"foo": "", "bar": "-", "baz": "N/A", "almafa": null, "kortefa": {"a":{"s":{"d":{}}}}, "szilvafa": [[[]]]};
            ignorecase_dict = dict;
            unset_empties(ignorecase_dict, targets=["n/a", "-", null, "", {}, []], recursive=true, ignorecase=true);

            $MSG = ignorecase_dict;
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == r"""{}"""


def test_unset_empties_replacement_dict(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
            dict = {"foo": "", "bar": "-", "baz": "N/A", "almafa": null, "kortefa": {"a":{"s":{"d":{}}}}, "szilvafa": [[[]]]};
            replacement_dict = dict;
            unset_empties(replacement_dict, targets=["n/a", "-", null, "", {}, []], recursive=true, ignorecase=true, replacement="do");

            $MSG = replacement_dict;
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == r"""{"foo":"do","bar":"do","baz":"do","almafa":"do","kortefa":{"a":{"s":{"d":"do"}}},"szilvafa":[["do"]]}"""


def test_unset_empties_defaults_list(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
            list = ["", "-", "N/A", null, {"a":{"s":{"d":{}}}}, [[[]]]];
            defaults_list = list;
            unset_empties(defaults_list, recursive=false);

            $MSG = format_json(defaults_list);
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == r"""["-","N/A",{"a":{"s":{"d":{}}}},[[[]]]]"""


def test_unset_empties_explicit_list(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
            list = ["", "-", "N/A", null, {"a":{"s":{"d":{}}}}, [[[]]]];
            explicit_list = list;
            unset_empties(explicit_list, recursive=true);

            $MSG = format_json(explicit_list);
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == r"""["-","N/A"]"""


def test_unset_empties_no_defaults_list(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
            list = ["", "-", "N/A", null, {"a":{"s":{"d":{}}}}, [[[]]]];
            no_defaults_list = list;
            unset_empties(no_defaults_list, targets=["n/a", "-"], recursive=true, ignorecase=true);

            $MSG = format_json(no_defaults_list);
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == r"""["",null,{"a":{"s":{"d":{}}}},[[[]]]]"""


def test_unset_empties_target_list(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
            list = ["", "-", "N/A", null, {"a":{"s":{"d":{}}}}, [[[]]]];
            target_list = list;
            unset_empties(target_list, targets=["n/a", "-", null, "", {}, []], recursive=true, ignorecase=false);

            $MSG = format_json(target_list);
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == r"""["N/A"]"""


def test_unset_empties_ignorecase_list(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
            list = ["", "-", "N/A", null, {"a":{"s":{"d":{}}}}, [[[]]]];
            ignorecase_list = list;
            unset_empties(ignorecase_list, targets=["n/a", "-", null, "", {}, []], recursive=true, ignorecase=true);

            $MSG = format_json(ignorecase_list);
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == r"""[]"""


def test_unset_empties_replacement_list(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
            list = ["", "-", "N/A", null, {"a":{"s":{"d":{}}}}, [[[]]]];
            replacement_list = list;
            unset_empties(replacement_list, targets=["n/a", "-", null, "", {}, []], recursive=true, ignorecase=true, replacement="do");

            $MSG = format_json(replacement_list);
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == r"""["do","do","do","do",{"a":{"s":{"d":"do"}}},[["do"]]]"""


def test_metrics_labels_ctor_with_dict(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
            labels = metrics_labels({
                "foo": "bar",
                "baz": {"inner_key": "inner_value"},
            });

            update_metric("test", labels=labels);
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1

    metric_filters = [MetricFilter("syslogng_test", {"foo": "bar", "baz": '{"inner_key":"inner_value"}'})]
    samples = config.prometheus_stats_handler.get_samples(metric_filters)
    assert len(samples) == 1


def test_metrics_labels_get(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
            $MSG = {};

            labels = metrics_labels();
            $MSG.empty_labels_does_not_exist = labels["empty-labels-does-not-exist"] ?? "fallback";

            labels["foo"] = "bar";
            $MSG.does_not_exist = labels["does-not-exist"] ?? "fallback";

            $MSG.exists = labels["foo"] ?? "fallback";
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == r"""{"empty_labels_does_not_exist":"fallback","does_not_exist":"fallback","exists":"bar"}"""


def test_metrics_labels_empty_value_and_null_value(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
            labels = metrics_labels({
                "foo": "bar",
                "empty_from_ctor": "",
                "null_from_ctor": null,
            });
            labels["empty_from_set_subscript"] = "";
            labels["null_from_set_subscript"] = null;

            update_metric("test", labels=labels);
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1

    metric_filters = [MetricFilter("syslogng_test", {"foo": "bar"})]
    samples = config.prometheus_stats_handler.get_samples(metric_filters)
    assert len(samples) == 1
    assert samples[0].labels == {"foo": "bar"}


def test_set_fields(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
    $MSG = {
        "foo": "foo_exists",
        "bar": "bar_exists",
        "bax": "to-be-replaced",
        "bax2": "to-be-replaced",
    };
    set_fields(
        $MSG,
        overrides={
            "foo": [invalid_expr, "foo_override"],
            "baz": "baz_override",
            "almafa": [invalid_expr_1, null],  # Should not have any effect, as there is no valid expr or non-null here.
        },
        defaults={
            "foo": [invalid_expr, "foo_default"],  # Should not have any effect, "foo" is handled by overrides.
            "bar": "bar_default",  # Should not have any effect, "bar" already has value in the dict.
            "almafa": "almafa_default",
            "kortefa": [invalid_expr_1, null],  # Should not have any effect, as there is no valid expr or non-null here.
        },
        replacements={
            "bax": "",
            "bax2": [invalid_expr, null, "non-null"],
        }
    );
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == '{"foo":"foo_override","bar":"bar_exists","bax":"","bax2":"non-null","baz":"baz_override","almafa":"almafa_default"}'


def compile_protobuf_schema(testcase_dir: Path, schema_file_path: Path) -> Path:
    descriptor_set_path = testcase_dir / "schema.pb"
    subprocess.run(
        [
            "protoc",
            f"--proto_path={testcase_dir}",
            f"--descriptor_set_out={descriptor_set_path}",
            str(schema_file_path),
        ],
        check=True,
    )
    return descriptor_set_path


def load_protobuf_data(testcase_dir: Path, schema_file_path: Path, protobuf_data: bytes) -> Message:
    descriptor_set_path = compile_protobuf_schema(testcase_dir, schema_file_path)

    with open(descriptor_set_path, "rb") as f:
        descriptor_set = descriptor_pb2.FileDescriptorSet()
        descriptor_set.ParseFromString(f.read())

    file_descriptor = descriptor_set.file[0]
    factory = message_factory.MessageFactory()
    message_descriptor = factory.pool.AddSerializedFile(file_descriptor.SerializeToString())
    TestMessage = message_factory.GetMessageClass(message_descriptor.message_types_by_name["TestMessage"])

    return TestMessage.FromString(protobuf_data)


@pytest.mark.skipif(shutil.which("protoc") is None, reason="protoc binary is not available")
def test_protobuf_message(testcase_dir, config, syslog_ng):
    schema_file_path = testcase_dir / "schema.proto"
    schema_file = File(schema_file_path)
    schema_file.write_content_and_close("""
    syntax = "proto3";

    message TestMessage {
        message InnerMessage {
            string inner_field = 1;
            sint64 inner_sint64_field = 2;
        }

        string string_field = 1;
        bytes bytes_field = 2;
        fixed32 fixed32_field = 3;
        fixed64 fixed64_field = 4;
        sfixed32 sfixed32_field = 5;
        sfixed64 sfixed64_field = 6;
        sint32 sint32_field = 7;
        sint64 sint64_field = 8;
        uint32 uint32_field = 9;
        uint64 uint64_field = 10;
        double double_field = 11;
        float float_field = 12;
        bool bool_field = 13;
        map<string, string> map_string_string_field = 14;
        InnerMessage inner_message_field = 15;

        repeated string repeated_string_field = 16;
        repeated bytes repeated_bytes_field = 17;
        repeated fixed32 repeated_fixed32_field = 18;
        repeated fixed64 repeated_fixed64_field = 19;
        repeated sfixed32 repeated_sfixed32_field = 20;
        repeated sfixed64 repeated_sfixed64_field = 21;
        repeated sint32 repeated_sint32_field = 22;
        repeated sint64 repeated_sint64_field = 23;
        repeated uint32 repeated_uint32_field = 24;
        repeated uint64 repeated_uint64_field = 25;
        repeated double repeated_double_field = 26;
        repeated float repeated_float_field = 27;
        repeated bool repeated_bool_field = 28;
        repeated InnerMessage repeated_inner_message_field = 29;
    }
    """)

    (file_final,) = create_config(
        config, r"""
    dict = {
        "string_field": "foo",
        "bytes_field": bytes("\x01\x02\x03\x04\x05"),
        "fixed32_field": 2147483647,
        "fixed64_field": 9223372036854775807,
        "sfixed32_field": -2147483648,
        "sfixed64_field": -9223372036854775808,
        "sint32_field": -2147483648,
        "sint64_field": -9223372036854775808,
        "uint32_field": 4294967295,
        "uint64_field": 9223372036854775807,
        "double_field": 17976931348623157.123456,
        "float_field": 123.456,
        "bool_field": true,
        "map_string_string_field": {"key1": "value1", "key2": "value2"},
        "inner_message_field": {
            "inner_field": "inner_value",
            "inner_sint64_field": -9223372036854775808,
        },

        "repeated_string_field": ["item1", "item2", "item3"],
        "repeated_bytes_field": [bytes("\x01\x02"), bytes("\x03\x04")],
        "repeated_fixed32_field": [1, 2, 3],
        "repeated_fixed64_field": [4, 5, 6],
        "repeated_sfixed32_field": [-1, -2, -3],
        "repeated_sfixed64_field": [-4, -5, -6],
        "repeated_sint32_field": [-7, -8, -9],
        "repeated_sint64_field": [-10, -11, -12],
        "repeated_uint32_field": [7, 8, 9],
        "repeated_uint64_field": [10, 11, 12],
        "repeated_double_field": [1.1, 2.2, 3.3],
        "repeated_float_field": [4.4, 5.5, 6.6],
        "repeated_bool_field": [true, false, true],
        "repeated_inner_message_field": [
            {"inner_field": "a", "inner_sint64_field": 1},
            {"inner_field": "b", "inner_sint64_field": 2},
        ],
    };

    """ + f'protobuf_data = protobuf_message(dict, schema_file="{str(schema_file_path)}");' + r"""

    $MSG = {
        "protobuf_data": protobuf_data,
    };
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    protobuf_data = base64.b64decode(json.loads(file_final.read_log())["protobuf_data"])

    protobuf_message = load_protobuf_data(testcase_dir, schema_file_path, protobuf_data)

    assert protobuf_message.string_field == "foo"
    assert protobuf_message.bytes_field == b"\x01\x02\x03\x04\x05"
    assert protobuf_message.fixed32_field == 2147483647
    assert protobuf_message.fixed64_field == 9223372036854775807
    assert protobuf_message.sfixed32_field == -2147483648
    assert protobuf_message.sfixed64_field == -9223372036854775808
    assert protobuf_message.sint32_field == -2147483648
    assert protobuf_message.sint64_field == -9223372036854775808
    assert protobuf_message.uint32_field == 4294967295
    assert protobuf_message.uint64_field == 9223372036854775807
    assert math.isclose(protobuf_message.float_field, 123.456, rel_tol=1e-6)
    assert math.isclose(protobuf_message.double_field, 17976931348623157.123456, rel_tol=1e-6)
    assert protobuf_message.bool_field is True
    assert protobuf_message.map_string_string_field["key1"] == "value1"
    assert protobuf_message.map_string_string_field["key2"] == "value2"
    assert protobuf_message.inner_message_field.inner_field == "inner_value"
    assert protobuf_message.inner_message_field.inner_sint64_field == -9223372036854775808

    assert protobuf_message.repeated_string_field == ["item1", "item2", "item3"]
    assert list(protobuf_message.repeated_bytes_field) == [b"\x01\x02", b"\x03\x04"]
    assert list(protobuf_message.repeated_fixed32_field) == [1, 2, 3]
    assert list(protobuf_message.repeated_fixed64_field) == [4, 5, 6]
    assert list(protobuf_message.repeated_sfixed32_field) == [-1, -2, -3]
    assert list(protobuf_message.repeated_sfixed64_field) == [-4, -5, -6]
    assert list(protobuf_message.repeated_sint32_field) == [-7, -8, -9]
    assert list(protobuf_message.repeated_sint64_field) == [-10, -11, -12]
    assert list(protobuf_message.repeated_uint32_field) == [7, 8, 9]
    assert list(protobuf_message.repeated_uint64_field) == [10, 11, 12]
    assert all(math.isclose(a, b, rel_tol=1e-6) for a, b in zip(protobuf_message.repeated_double_field, [1.1, 2.2, 3.3]))
    assert all(math.isclose(a, b, rel_tol=1e-6) for a, b in zip(protobuf_message.repeated_float_field, [4.4, 5.5, 6.6]))
    assert list(protobuf_message.repeated_bool_field) == [True, False, True]
    assert len(protobuf_message.repeated_inner_message_field) == 2
    assert protobuf_message.repeated_inner_message_field[0].inner_field == "a"
    assert protobuf_message.repeated_inner_message_field[0].inner_sint64_field == 1
    assert protobuf_message.repeated_inner_message_field[1].inner_field == "b"
    assert protobuf_message.repeated_inner_message_field[1].inner_sint64_field == 2


def test_format_cef(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
    parsed_cef = {
        "cef_version": "0",
        "device_vendor": "dummy|device|containing|header|separator|that|needs|to|be|backslash|escaped",
        "device_product": "dummy\\device\\product\\containing\\backslash\\that\\needs\\to\\be\\backslash\\escaped",
        "device_version": "dummy device version containing extension pair separator that does not need to be escaped",
        "device_event_class_id": "1234",
        "event_name": "dummy=name=containing=extension=value=separator=that=does=not=need=to=be=escaped",
        "agent_severity": "5",
        "dummy_key1": "value",
        "dummy_key2": "value containing pair separators that does not need to be escaped",
        "dummy_key3": "value=containing=value=separators=that=needs=to=be=escaped",
        "dummy_key4": "value\\containing\\backslash\\that\\needs\\to\\be\\backslash\\escaped",
    };
    $MSG = format_cef(parsed_cef);
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == (
        "CEF:0|"
        "dummy\\|device\\|containing\\|header\\|separator\\|that\\|needs\\|to\\|be\\|backslash\\|escaped|"
        "dummy\\\\device\\\\product\\\\containing\\\\backslash\\\\that\\\\needs\\\\to\\\\be\\\\backslash\\\\escaped|"
        "dummy device version containing extension pair separator that does not need to be escaped|"
        "1234|"
        "dummy=name=containing=extension=value=separator=that=does=not=need=to=be=escaped|"
        "5|"
        "dummy_key1=value "
        "dummy_key2=value containing pair separators that does not need to be escaped "
        "dummy_key3=value\\=containing\\=value\\=separators\\=that\\=needs\\=to\\=be\\=escaped "
        "dummy_key4=value\\\\containing\\\\backslash\\\\that\\\\needs\\\\to\\\\be\\\\backslash\\\\escaped"
    )


def test_format_leef_version_1(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
    parsed_leef = {
        "leef_version": "1.0",
        "vendor_name": "dummy|vendor|containing|header|separator|that|needs|to|be|backslash|escaped",
        "product_name": "dummy\\product\\name\\containing\\backslash\\that\\needs\\to\\be\\backslash\\escaped",
        "product_version": "dummy\tdevice\tversion\tcontaining\textension\tpair\tseparator\tthat\tdoes\tnot\tneed\tto\tbe\tescaped",
        "event_id": "1234",
        "dummy_key1": "value",
        "dummy_key2": "value\tcontaining\tpair\tseparators\tthat\tdoes\tnot\tneed\tto\tbe\tescaped",
        "dummy_key3": "value=containing=value=separators=that=needs=to=be=escaped",
        "dummy_key4": "value\\containing\\backslash\\that\\needs\\to\\be\\backslash\\escaped",
    };
    $MSG = format_leef(parsed_leef);
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == (
        "LEEF:1.0|"
        "dummy\\|vendor\\|containing\\|header\\|separator\\|that\\|needs\\|to\\|be\\|backslash\\|escaped|"
        "dummy\\\\product\\\\name\\\\containing\\\\backslash\\\\that\\\\needs\\\\to\\\\be\\\\backslash\\\\escaped|"
        "dummy\tdevice\tversion\tcontaining\textension\tpair\tseparator\tthat\tdoes\tnot\tneed\tto\tbe\tescaped|"
        "1234|"
        "dummy_key1=value\t"
        "dummy_key2=value\tcontaining\tpair\tseparators\tthat\tdoes\tnot\tneed\tto\tbe\tescaped\t"
        "dummy_key3=value\\=containing\\=value\\=separators\\=that\\=needs\\=to\\=be\\=escaped\t"
        "dummy_key4=value\\\\containing\\\\backslash\\\\that\\\\needs\\\\to\\\\be\\\\backslash\\\\escaped"
    )


def test_format_leef_version_2_no_delimiter(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
    parsed_leef = {
        "leef_version": "2.0",
        "vendor_name": "dummy|vendor|containing|header|separator|that|needs|to|be|backslash|escaped",
        "product_name": "dummy\\product\\name\\containing\\backslash\\that\\needs\\to\\be\\backslash\\escaped",
        "product_version": "dummy\tdevice\tversion\tcontaining\textension\tpair\tseparator\tthat\tdoes\tnot\tneed\tto\tbe\tescaped",
        "event_id": "1234",
        "dummy_key1": "value",
        "dummy_key2": "value\tcontaining\tpair\tseparators\tthat\tdoes\tnot\tneed\tto\tbe\tescaped",
        "dummy_key3": "value=containing=value=separators=that=needs=to=be=escaped",
        "dummy_key4": "value\\containing\\backslash\\that\\needs\\to\\be\\backslash\\escaped",
    };
    $MSG = format_leef(parsed_leef);
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == (
        "LEEF:2.0|"
        "dummy\\|vendor\\|containing\\|header\\|separator\\|that\\|needs\\|to\\|be\\|backslash\\|escaped|"
        "dummy\\\\product\\\\name\\\\containing\\\\backslash\\\\that\\\\needs\\\\to\\\\be\\\\backslash\\\\escaped|"
        "dummy\tdevice\tversion\tcontaining\textension\tpair\tseparator\tthat\tdoes\tnot\tneed\tto\tbe\tescaped|"
        "1234|"
        "\t|"
        "dummy_key1=value\t"
        "dummy_key2=value\tcontaining\tpair\tseparators\tthat\tdoes\tnot\tneed\tto\tbe\tescaped\t"
        "dummy_key3=value\\=containing\\=value\\=separators\\=that\\=needs\\=to\\=be\\=escaped\t"
        "dummy_key4=value\\\\containing\\\\backslash\\\\that\\\\needs\\\\to\\\\be\\\\backslash\\\\escaped"
    )


def test_format_leef_version_2_empty_delimiter(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
    parsed_leef = {
        "leef_version": "2.0",
        "vendor_name": "dummy|vendor|containing|header|separator|that|needs|to|be|backslash|escaped",
        "product_name": "dummy\\product\\name\\containing\\backslash\\that\\needs\\to\\be\\backslash\\escaped",
        "product_version": "dummy\tdevice\tversion\tcontaining\textension\tpair\tseparator\tthat\tdoes\tnot\tneed\tto\tbe\tescaped",
        "event_id": "1234",
        "leef_delimiter": "",
        "dummy_key1": "value",
        "dummy_key2": "value\tcontaining\tpair\tseparators\tthat\tdoes\tnot\tneed\tto\tbe\tescaped",
        "dummy_key3": "value=containing=value=separators=that=needs=to=be=escaped",
        "dummy_key4": "value\\containing\\backslash\\that\\needs\\to\\be\\backslash\\escaped",
    };
    $MSG = format_leef(parsed_leef);
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == (
        "LEEF:2.0|"
        "dummy\\|vendor\\|containing\\|header\\|separator\\|that\\|needs\\|to\\|be\\|backslash\\|escaped|"
        "dummy\\\\product\\\\name\\\\containing\\\\backslash\\\\that\\\\needs\\\\to\\\\be\\\\backslash\\\\escaped|"
        "dummy\tdevice\tversion\tcontaining\textension\tpair\tseparator\tthat\tdoes\tnot\tneed\tto\tbe\tescaped|"
        "1234|"
        "\t|"
        "dummy_key1=value\t"
        "dummy_key2=value\tcontaining\tpair\tseparators\tthat\tdoes\tnot\tneed\tto\tbe\tescaped\t"
        "dummy_key3=value\\=containing\\=value\\=separators\\=that\\=needs\\=to\\=be\\=escaped\t"
        "dummy_key4=value\\\\containing\\\\backslash\\\\that\\\\needs\\\\to\\\\be\\\\backslash\\\\escaped"
    )


def test_format_leef_version_2_space_delimiter(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
    parsed_leef = {
        "leef_version": "2.0",
        "vendor_name": "dummy|vendor|containing|header|separator|that|needs|to|be|backslash|escaped",
        "product_name": "dummy\\product\\name\\containing\\backslash\\that\\needs\\to\\be\\backslash\\escaped",
        "product_version": "dummy\tdevice\tversion\tcontaining\textension\tpair\tseparator\tthat\tdoes\tnot\tneed\tto\tbe\tescaped",
        "event_id": "1234",
        "leef_delimiter": " ",
        "dummy_key1": "value",
        "dummy_key2": "value containing pair separators that does not need to be escaped",
        "dummy_key3": "value=containing=value=separators=that=needs=to=be=escaped",
        "dummy_key4": "value\\containing\\backslash\\that\\needs\\to\\be\\backslash\\escaped",
    };
    $MSG = format_leef(parsed_leef);
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == (
        "LEEF:2.0|"
        "dummy\\|vendor\\|containing\\|header\\|separator\\|that\\|needs\\|to\\|be\\|backslash\\|escaped|"
        "dummy\\\\product\\\\name\\\\containing\\\\backslash\\\\that\\\\needs\\\\to\\\\be\\\\backslash\\\\escaped|"
        "dummy\tdevice\tversion\tcontaining\textension\tpair\tseparator\tthat\tdoes\tnot\tneed\tto\tbe\tescaped|"
        "1234|"
        " |"
        "dummy_key1=value "
        "dummy_key2=value containing pair separators that does not need to be escaped "
        "dummy_key3=value\\=containing\\=value\\=separators\\=that\\=needs\\=to\\=be\\=escaped "
        "dummy_key4=value\\\\containing\\\\backslash\\\\that\\\\needs\\\\to\\\\be\\\\backslash\\\\escaped"
    )


def test_str_replace(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
    dal = "érik a szőlő, hajlik a vessző";
    $MSG = {
        "replace_all": str_replace(dal, "a", "egy"),
        "replace_some": str_replace(dal, "a", "egy", 1)
    };
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == '{"replace_all":"érik egy szőlő, hegyjlik egy vessző","replace_some":"érik egy szőlő, hajlik a vessző"}'


def test_str_strip(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
    madar = " \n\r\t  szirti \t sas  \n\r\t";
    $MSG = {
        "strip": str_strip(madar),
        "lstrip": str_lstrip(madar),
        "rstrip": str_rstrip(madar),
    };
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == '{"strip":"szirti \\t sas","lstrip":"szirti \\t sas  \\n\\r\\t","rstrip":" \\n\\r\\t  szirti \\t sas"}'


def test_dict_to_pairs(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
    dict = {
        "value_1": "foo",
        "value_2": "bar",
        "value_3": ["baz", "bax"],
    };
    $MSG = dict_to_pairs(dict, "key", "value");
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == '[{"key":"value_1","value":"foo"},{"key":"value_2","value":"bar"},{"key":"value_3","value":["baz","bax"]}]'


def test_flatten(config, syslog_ng):
    (file_final,) = create_config(
        config, r"""
            dict = {"top_level_field":42,"top_level_dict":{"inner_field":1337,"inner_dict":{"inner_inner_field":1}}};

            default_separator = dict;
            custom_separator = dict;

            flatten(default_separator);
            flatten(custom_separator, separator="->");

            $MSG = [default_separator, custom_separator];
    """,
    )
    syslog_ng.start(config)

    assert file_final.get_stats()["processed"] == 1
    assert file_final.read_log() == '[' \
        '{"top_level_field":42,"top_level_dict.inner_field":1337,"top_level_dict.inner_dict.inner_inner_field":1},' \
        '{"top_level_field":42,"top_level_dict->inner_field":1337,"top_level_dict->inner_dict->inner_inner_field":1}' \
        ']'
