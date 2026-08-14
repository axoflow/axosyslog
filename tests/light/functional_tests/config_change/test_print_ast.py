#!/usr/bin/env python
#############################################################################
# Copyright (c) 2026 Axoflow
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


def _set_raw_config(config, body):
    config.set_raw_config("@version: {}\n".format(config.get_version()) + body)


def test_print_ast_reports_every_log_expression(config, syslog_ng):
    _set_raw_config(
        config,
        """
options { keep-hostname(yes); };
template t_x { template("$MSG\\n"); };
source s_in { internal(); };
log {
    source(s_in);
    filterx {
        $MSG = "hello";
    };
    destination { file("/dev/null"); };
};
""",
    )

    ast = syslog_ng.print_ast(config)

    statements = ast["statements"]
    # options {} and template {} are not part of the cfg-tree, so they are not
    # reported, not even the options {} the light config renderer appends
    assert [statement["kind"] for statement in statements] == ["source", "log"]
    assert statements[0]["expr"]["name"] == "s_in"

    log_path = statements[1]
    assert log_path["expr"]["layout"] == "sequence"

    # source(s_in); filterx {}; destination {};
    children = log_path["expr"]["children"]
    assert len(children) == 3
    assert children[0]["layout"] == "reference"
    assert children[0]["content"] == "source"
    assert children[1]["content"] == "filter"

    filterx_pipe = children[1]["children"][0]
    assert filterx_pipe["plugin"] == "filterx"
    assert filterx_pipe["filterx"]["type"] == "compound"
    assert filterx_pipe["filterx"]["children"][0]["text"] == '$MSG = "hello"'


def test_print_ast_of_a_broken_config_fails(config, syslog_ng):
    _set_raw_config(config, "log { this-is-not-a-driver(); };\n")

    with pytest.raises(Exception, match="--print-ast failed"):
        syslog_ng.print_ast(config)
