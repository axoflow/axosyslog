#!/usr/bin/env python
#############################################################################
# Copyright (c) 2026 Axoflow
# Copyright (c) 2026 Balazs Scheidler <balazs.scheidler@axoflow.com>
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


def create_config(config, port_allocator, aggregate_call):
    network_source = config.create_network_source(port=port_allocator(), flags="no-parse")
    file_destination = config.create_file_destination(file_name="output.log", template="'$MSG\n'")

    raw_config = f"""
@version: {config.get_version()}

options {{ stats(level(1)); }};

source s_net {{
    {render_statement(network_source)};
}};

destination d_file {{
    {render_statement(file_destination)};
}};

log {{
    source(s_net);
    filterx {{
        input = parse_json($MSG);
        (status, values) = {aggregate_call};
        $MSG = json();
        $MSG.status = status;
        $MSG.values = values;
    }};
    destination(d_file);
}};
"""
    config.set_raw_config(raw_config)
    return network_source, file_destination


def send_messages(network_source, messages):
    network_source.write_logs([json.dumps(message) for message in messages])


def read_results(file_destination, count):
    return [json.loads(line) for line in file_destination.read_logs(count)]


def test_basic_aggregation_sums_values_for_repeated_key(config, port_allocator, syslog_ng):
    network_source, file_destination = create_config(
        config, port_allocator,
        'aggregate(key=(input["key"]), values={"cnt": input["cnt"]})',
    )
    syslog_ng.start(config)

    send_messages(
        network_source, [
            {"key": "host-a", "cnt": 1},
            {"key": "host-a", "cnt": 2},
        ],
    )

    results = read_results(file_destination, 2)
    assert results[0] == {"status": "absorbed", "values": {"cnt": 1}}
    assert results[1] == {"status": "absorbed", "values": {"cnt": 3}}


def test_basic_aggregation_keeps_separate_state_per_key(config, port_allocator, syslog_ng):
    network_source, file_destination = create_config(
        config, port_allocator,
        'aggregate(key=(input["key"]), values={"cnt": input["cnt"]})',
    )
    syslog_ng.start(config)

    send_messages(
        network_source, [
            {"key": "host-a", "cnt": 1},
            {"key": "host-b", "cnt": 10},
            {"key": "host-a", "cnt": 1},
        ],
    )

    results = read_results(file_destination, 3)
    assert results[0] == {"status": "absorbed", "values": {"cnt": 1}}
    assert results[1] == {"status": "absorbed", "values": {"cnt": 10}}
    assert results[2] == {"status": "absorbed", "values": {"cnt": 2}}


def test_basic_aggregation_merges_multiple_fields_independently(config, port_allocator, syslog_ng):
    network_source, file_destination = create_config(
        config, port_allocator,
        'aggregate(key=(input["key"]), values={"a": input["a"], "b": input["b"]})',
    )
    syslog_ng.start(config)

    send_messages(
        network_source, [
            {"key": "host-a", "a": 1, "b": 100},
            {"key": "host-a", "a": 2, "b": 200},
        ],
    )

    results = read_results(file_destination, 2)
    assert results[0] == {"status": "absorbed", "values": {"a": 1, "b": 100}}
    assert results[1] == {"status": "absorbed", "values": {"a": 3, "b": 300}}


def test_close_argument_finalizes_entry_and_starts_fresh_afterwards(config, port_allocator, syslog_ng):
    network_source, file_destination = create_config(
        config, port_allocator,
        'aggregate(key=(input["key"]), values={"cnt": input["cnt"]}, close=input["close"])',
    )
    syslog_ng.start(config)

    send_messages(
        network_source, [
            {"key": "host-a", "cnt": 1, "close": False},
            {"key": "host-a", "cnt": 2, "close": True},
            {"key": "host-a", "cnt": 5, "close": False},
        ],
    )

    results = read_results(file_destination, 3)
    assert results[0] == {"status": "absorbed", "values": {"cnt": 1}}
    assert results[1] == {"status": "closed", "values": {"cnt": 3}}
    assert results[2] == {"status": "absorbed", "values": {"cnt": 5}}


def test_close_argument_only_affects_the_closed_key(config, port_allocator, syslog_ng):
    network_source, file_destination = create_config(
        config, port_allocator,
        'aggregate(key=(input["key"]), values={"cnt": input["cnt"]}, close=input["close"])',
    )
    syslog_ng.start(config)

    send_messages(
        network_source, [
            {"key": "host-a", "cnt": 1, "close": False},
            {"key": "host-b", "cnt": 10, "close": False},
            {"key": "host-a", "cnt": 1, "close": True},
            {"key": "host-b", "cnt": 10, "close": False},
        ],
    )

    results = read_results(file_destination, 4)
    assert results[0] == {"status": "absorbed", "values": {"cnt": 1}}
    assert results[1] == {"status": "absorbed", "values": {"cnt": 10}}
    assert results[2] == {"status": "closed", "values": {"cnt": 2}}
    assert results[3] == {"status": "absorbed", "values": {"cnt": 20}}
