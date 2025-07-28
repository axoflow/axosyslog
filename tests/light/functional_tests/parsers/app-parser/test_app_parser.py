#!/usr/bin/env python
#############################################################################
# Copyright (c) 2019 Balabit
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
import pytest


test_parameters_first_match = [
    ("""foo foomessage""", "${.app.name}", "foo"),
    ("""bar barmessage""", "${.app.name}", "bar"),
    ("""foobar foobarmessage""", "${.app.name}", "foo"),
]


test_parameters_all_matches = [
    ("""foo foomessage""", "${FOOVALUE} ${BARVALUE}", "foo "),
    ("""bar barmessage""", "${FOOVALUE} ${BARVALUE}", " bar"),
    ("""foobar foobarmessage""", "${FOOVALUE} ${BARVALUE}", "foo bar"),
]


preamble_foobar_apps = """
application foo[syslog] {
    filter { program("foo"); };
    parser {
        channel {
            rewrite { set("foo" value("FOOVALUE"));  };
        };
    };
};

application bar[syslog] {
    filter { program("bar"); };
    parser {
        channel {
            rewrite { set("bar" value("BARVALUE")); };
        };
    };
};
"""


@pytest.mark.parametrize(
    "input_message, template, expected_value", test_parameters_first_match,
    ids=list(map(str, range(len(test_parameters_first_match)))),
)
def test_app_parser_first_match_without_overlaps_only_traverses_the_first_app(config, syslog_ng, input_message, template, expected_value):
    config.add_preamble(preamble_foobar_apps)
    generator_source = config.create_example_msg_generator_source(num=1, template=config.stringify(input_message))
    syslog_parser = config.create_syslog_parser(flags="syslog-protocol")
    app_parser = config.create_app_parser(topic="syslog")
    file_destination = config.create_file_destination(file_name="output.log", template=config.stringify(template + '\n'))
    config.create_logpath(statements=[generator_source, syslog_parser, app_parser, file_destination])

    syslog_ng.start(config)
    assert file_destination.read_log().strip() == expected_value


@pytest.mark.parametrize(
    "input_message, template, expected_value", test_parameters_all_matches,
    ids=list(map(str, range(len(test_parameters_all_matches)))),
)
def test_app_parser_allow_overlaps_causes_all_apps_to_be_traversed(config, syslog_ng, input_message, template, expected_value):
    config.add_preamble(preamble_foobar_apps)
    generator_source = config.create_example_msg_generator_source(num=1, template=config.stringify(input_message))
    syslog_parser = config.create_syslog_parser(flags="syslog-protocol")
    app_parser = config.create_app_parser(topic="syslog", allow_overlaps="yes")
    file_destination = config.create_file_destination(file_name="output.log", template=config.stringify(template + '\n'))
    config.create_logpath(statements=[generator_source, syslog_parser, app_parser, file_destination])

    syslog_ng.start(config)
    assert file_destination.read_log() == expected_value


def test_app_parser_dont_match_causes_the_message_to_be_dropped(config, syslog_ng):
    config.add_preamble(preamble_foobar_apps)
    config.update_global_options(stats_level=1)
    generator_source = config.create_example_msg_generator_source(num=1, template=config.stringify("almafa message"))
    syslog_parser = config.create_syslog_parser(flags="syslog-protocol")
    app_parser = config.create_app_parser(topic="syslog")
    file_destination = config.create_file_destination(file_name="output.log", template=config.stringify("foobar"))
    config.create_logpath(statements=[generator_source, syslog_parser, app_parser, file_destination])

    syslog_ng.start(config)
    assert "processed" not in file_destination.get_stats()


def test_app_parser_auto_parse_disabled_causes_the_message_to_be_dropped(config, syslog_ng):
    config.add_preamble(preamble_foobar_apps)
    config.update_global_options(stats_level=1)
    generator_source = config.create_example_msg_generator_source(num=1, template=config.stringify("foo foomessage"))
    syslog_parser = config.create_syslog_parser(flags="syslog-protocol")
    app_parser = config.create_app_parser(topic="syslog", auto_parse="no")
    file_destination = config.create_file_destination(file_name="output.log")
    config.create_logpath(statements=[generator_source, syslog_parser, app_parser, file_destination])

    syslog_ng.start(config)
    assert "processed" not in file_destination.get_stats()


def test_app_parser_auto_parse_disabled_plus_overlaps_causes_the_message_to_be_accepted(config, syslog_ng):
    config.add_preamble(preamble_foobar_apps)
    config.update_global_options(stats_level=1)
    generator_source = config.create_example_msg_generator_source(num=1, template=config.stringify("foo foomessage"))
    syslog_parser = config.create_syslog_parser(flags="syslog-protocol")
    app_parser = config.create_app_parser(topic="syslog", auto_parse="no", allow_overlaps="yes")
    file_destination = config.create_file_destination(file_name="output.log")
    config.create_logpath(statements=[generator_source, syslog_parser, app_parser, file_destination])

    syslog_ng.start(config)
    assert "processed" not in file_destination.get_stats()


def test_app_parser_not_overwriting_inner_adapters_result(config, syslog_ng):
    config.add_preamble("""
application inner-app[inner-topic] {
    filterx { $MSG == "for-the-inner"; };
};

application outer-app[outer-topic] {
  parser {
    channel {
        if {
            parser { app-parser(topic(inner-topic) filterx-app-variable(app.name)); };
        } else {
            filterx { $MSG == "for-the-outer"; };
        };
    };
  };
};
""")
    file_source = config.create_file_source(file_name="input.log", flags="no-parse")
    filterx_1 = config.create_filterx(r"declare app = {};")
    app_parser = config.create_app_parser(topic="outer-topic", filterx_app_variable="app.name")
    filterx_2 = config.create_filterx(r"$app_name = app.name;")
    file_destination = config.create_file_destination(file_name="output.log", template='"$app_name\\n"')
    config.create_logpath(statements=[file_source, filterx_1, app_parser, filterx_2, file_destination])

    syslog_ng.start(config)

    file_source.write_logs(["for-the-inner", "for-the-outer"])
    file_destination.read_logs(2) == ["inner-app", "outer-app"]

    syslog_ng.stop()
