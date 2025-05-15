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
from axosyslog_light.syslog_ng_ctl.prometheus_stats_handler import MetricFilter


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
