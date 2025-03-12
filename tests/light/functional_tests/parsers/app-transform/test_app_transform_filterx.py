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

import pytest

from src.syslog_ng_config.renderer import render_statement


def create_config(config, app, app_transform_options):
    file = config.create_file_destination(file_name="output.log", template='"$MSG\n"')

    raw_cfg = """
transformation filterx_only_1[default] {
    transform[filterx_1] {
        step["step_1"] {
            filterx {
                $MSG.filterx_only_1___filterx_1___step_1 = true;
            };
        };
        step["step_2"] {
            filterx {
                $MSG.filterx_only_1___filterx_1___step_2 = true;
            };
        };
    };
    transform[filterx_2] {
        step["step_1"] {
            filterx {
                $MSG.filterx_only_1___filterx_2___step_1 = true;
            };
        };
        step["step_2"] {
            filterx {
                $MSG.filterx_only_1___filterx_2___step_2 = true;
            };
        };
    };
};

transformation filterx_only_2[default] {
    transform[filterx_1] {
        step["step_1"] {
            filterx {
                $MSG.filterx_only_2___filterx_1___step_1 = true;
            };
        };
        step["step_2"] {
            filterx {
                $MSG.filterx_only_2___filterx_1___step_2 = true;
            };
        };
    };
    transform[filterx_2] {
        step["step_1"] {
            filterx {
                $MSG.filterx_only_2___filterx_2___step_1 = true;
            };
        };
        step["step_2"] {
            filterx {
                $MSG.filterx_only_2___filterx_2___step_2 = true;
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
        "filterx_only_1",
        "",
        {
            "filterx_only_1___filterx_1___step_1": True,
            "filterx_only_1___filterx_1___step_2": True,
            "filterx_only_1___filterx_2___step_1": True,
            "filterx_only_1___filterx_2___step_2": True,
        },
    ),
    (
        "filterx_only_1",
        "include-transforms(filterx_1)",
        {
            "filterx_only_1___filterx_1___step_1": True,
            "filterx_only_1___filterx_1___step_2": True,
        },
    ),
    (
        "filterx_only_2",
        "",
        {
            "filterx_only_2___filterx_1___step_1": True,
            "filterx_only_2___filterx_1___step_2": True,
            "filterx_only_2___filterx_2___step_1": True,
            "filterx_only_2___filterx_2___step_2": True,
        },
    ),
    (
        "filterx_only_2",
        "include-transforms(filterx_1)",
        {
            "filterx_only_2___filterx_1___step_1": True,
            "filterx_only_2___filterx_1___step_2": True,
        },
    ),
]


@pytest.mark.parametrize(
    "app, app_transform_options, expected_output",
    test_app_transform_parser_and_filterx_cases,
    ids=[f"{test[0]}_{test[1]}" for test in test_app_transform_parser_and_filterx_cases],
)
def test_app_transform_filterx(config, syslog_ng, app, app_transform_options, expected_output):
    file = create_config(config, app, app_transform_options)
    syslog_ng.start(config)
    assert json.loads(file.read_log()) == expected_output
