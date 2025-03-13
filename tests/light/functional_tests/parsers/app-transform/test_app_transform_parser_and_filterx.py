#!/usr/bin/env python
#############################################################################
# Copyright (c) 2025 Axoflow
# Copyright (c) 2025 Attila Szakacs <attila.szakacs@axoflow.com>
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
from pathlib import Path

import pytest

from src.syslog_ng_config.renderer import render_statement


def create_config(config, app, app_transform_options):
    file = config.create_file_destination(file_name="output.log", template='"$MSG\n"')

    raw_cfg = """
transformation filterx_only[default] {
    transform[filterx_1] {
        step["step_1"] {
            filterx {
                $MSG.filterx_only___filterx_1___step_1 = true;
            };
        };
        step["step_2"] {
            filterx {
                $MSG.filterx_only___filterx_1___step_2 = true;
            };
        };
    };
    transform[filterx_2] {
        step["step_1"] {
            filterx {
                $MSG.filterx_only___filterx_2___step_1 = true;
            };
        };
        step["step_2"] {
            filterx {
                $MSG.filterx_only___filterx_2___step_2 = true;
            };
        };
    };
};

transformation parser_only[default] {
    transform[parser_1] {
        step["step_1"] {
            parser {
                json-parser(template('{"parser_only___parser_1___step_1": true}') prefix(".json."));
            };
        };
        step["step_2"] {
            parser {
                json-parser(template('{"parser_only___parser_1___step_2": true}') prefix(".json."));
            };
        };
    };
    transform[parser_2] {
        step["step_1"] {
            parser {
                json-parser(template('{"parser_only___parser_2___step_1": true}') prefix(".json."));
            };
        };
        step["step_2"] {
            parser {
                json-parser(template('{"parser_only___parser_2___step_2": true}') prefix(".json."));
            };
        };
    };
};

transformation mixed[default] {
    transform[filterx_1] {
        step["step_1"] {
            filterx {
                $MSG.mixed___filterx_1___step_1 = true;
            };
        };
        step["step_2"] {
            filterx {
                $MSG.mixed___filterx_1___step_2 = true;
            };
        };
    };
    transform[parser_1] {
        step["step_1"] {
            parser {
                json-parser(template('{"mixed___parser_1___step_1": true}') prefix(".json."));
            };
        };
        step["step_2"] {
            parser {
                json-parser(template('{"mixed___parser_1___step_2": true}') prefix(".json."));
            };
        };
    };
    transform[mixed_1] {
        step["step_1"] {
            filterx {
                $MSG.mixed___mixed_1___step_1 = true;
            };
        };
        step["step_2"] {
            parser {
                json-parser(template('{"mixed___mixed_1___step_2": true}') prefix(".json."));
            };
        };
    };
};

destination d_file {
    %s;
};

log {
    source {
        example-msg-generator(num(1));
    };
    filterx {
        declare app = "%s";
        $MSG = json();
    };
    parser {
        app-transform(filterx-app-variable(app) topic(default) %s);
    };
    rewrite {
        set("$(format-json .json.* --shift-levels 2)" value(logmsg_json));
    };
    filterx {
        logmsg_json = json($logmsg_json);
        $MSG += logmsg_json;
    };
    destination(d_file);
};
""" % (render_statement(file), app, app_transform_options)

    config.set_raw_config(raw_cfg)
    return file


test_app_transform_parser_and_filterx_cases = [
    (
        "non_existing_app",
        "",
        dict(),
    ),
    (
        "filterx_only",
        "",
        {
            "filterx_only___filterx_1___step_1": True,
            "filterx_only___filterx_1___step_2": True,
            "filterx_only___filterx_2___step_1": True,
            "filterx_only___filterx_2___step_2": True,
        },
    ),
    (
        "filterx_only",
        "include-transforms(filterx_1)",
        {
            "filterx_only___filterx_1___step_1": True,
            "filterx_only___filterx_1___step_2": True,
        },
    ),
    (
        "parser_only",
        "",
        {
            "parser_only___parser_1___step_1": True,
            "parser_only___parser_1___step_2": True,
            "parser_only___parser_2___step_1": True,
            "parser_only___parser_2___step_2": True,
        },
    ),
    (
        "parser_only",
        "include-transforms(parser_1)",
        {
            "parser_only___parser_1___step_1": True,
            "parser_only___parser_1___step_2": True,
        },
    ),
    (
        "mixed",
        "",
        {
            "mixed___filterx_1___step_1": True,
            "mixed___filterx_1___step_2": True,
            "mixed___parser_1___step_1": True,
            "mixed___parser_1___step_2": True,
            "mixed___mixed_1___step_1": True,
            "mixed___mixed_1___step_2": True,
        },
    ),
    (
        "mixed",
        "include-transforms(filterx_1)",
        {
            "mixed___filterx_1___step_1": True,
            "mixed___filterx_1___step_2": True,
        },
    ),
    (
        "mixed",
        "include-transforms(parser_1)",
        {
            "mixed___parser_1___step_1": True,
            "mixed___parser_1___step_2": True,
        },
    ),
    (
        "mixed",
        "include-transforms(mixed_1)",
        {
            "mixed___mixed_1___step_1": True,
            "mixed___mixed_1___step_2": True,
        },
    ),
]


@pytest.mark.parametrize(
    "app, app_transform_options, expected_output",
    test_app_transform_parser_and_filterx_cases,
    ids=[f"{test[0]}_{test[1]}" for test in test_app_transform_parser_and_filterx_cases],
)
def test_app_transform_parser_and_filterx(config, syslog_ng, app, app_transform_options, expected_output):
    file = create_config(config, app, app_transform_options)
    syslog_ng.start(config)
    assert json.loads(file.read_log()) == expected_output


# Testing the optimization:
#
# If a transformation has both parser and filterx transforms,
# but we only include the filterx transform,
# the generated code puts the filterx steps into a filterx switch-case.
def test_app_transform_mixed_filterx_optimization(config, syslog_ng):
    _ = create_config(config, "mixed", "include-transforms(filterx_1)")

    expected_lines = [
        "#Start Application mixed",
        "            case 'mixed':",
        "                # step: step_1",
        "                $MSG.mixed___filterx_1___step_1 = true;",
        "                # step: step_2",
        "                $MSG.mixed___filterx_1___step_2 = true;",
        "                break;",
        "#End Application mixed",
    ]

    preprocessed_config_path = Path("syslog_ng_server_preprocessed.conf")
    syslog_ng.start_params.preprocess_into = preprocessed_config_path
    syslog_ng.start(config)

    preprocessed_config = preprocessed_config_path.read_text()

    index = 0
    for line in expected_lines:
        index = preprocessed_config.find(line, index)
        assert index != -1
