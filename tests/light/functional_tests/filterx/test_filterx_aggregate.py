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
import time

import pytest
from axosyslog_light.syslog_ng_config.renderer import render_statement


def create_config(config, port_allocator, aggregate_call, port=None):
    network_source = config.create_network_source(port=port or port_allocator(), flags="no-parse")
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
        'aggregate(key=(input["key"]), values={"cnt": input["cnt"]}, timeout=3600)',
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
        'aggregate(key=(input["key"]), values={"cnt": input["cnt"]}, timeout=3600)',
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
        'aggregate(key=(input["key"]), values={"a": input["a"], "b": input["b"]}, timeout=3600)',
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
        'aggregate(key=(input["key"]), values={"cnt": input["cnt"]}, close=input["close"], timeout=3600)',
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
        'aggregate(key=(input["key"]), values={"cnt": input["cnt"]}, close=input["close"], timeout=3600)',
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


@pytest.mark.timing
def test_timeout_expires_entry_and_emits_accumulated_values(config, port_allocator, syslog_ng):
    network_source, file_destination = create_config(
        config, port_allocator,
        'aggregate(key=(input["key"]), values={"cnt": input["cnt"]}, timeout=1)',
    )
    syslog_ng.start(config)

    send_messages(network_source, [{"key": "host-a", "cnt": 7}])

    results = read_results(file_destination, 1)
    assert results[0] == {"status": "absorbed", "values": {"cnt": 7}}

    results = read_results(file_destination, 1)
    assert results[0] == {"status": "timeout", "values": {"cnt": 7}}


@pytest.mark.timing
def test_close_argument_cancels_the_pending_timeout(config, port_allocator, syslog_ng):
    network_source, file_destination = create_config(
        config, port_allocator,
        'aggregate(key=(input["key"]), values={"cnt": input["cnt"]}, close=input["close"], timeout=1)',
    )
    syslog_ng.start(config)

    send_messages(
        network_source, [
            {"key": "host-a", "cnt": 1, "close": False},
            {"key": "host-a", "cnt": 2, "close": True},
        ],
    )

    results = read_results(file_destination, 2)
    assert results[1]["status"] == "closed"

    # the timer that was armed for the first message must have been
    # cancelled by the close, otherwise a spurious third ("timeout") message
    # would show up here
    time.sleep(1.5)
    assert file_destination.get_stats()["processed"] == 2


@pytest.mark.timing
def test_timeout_is_armed_only_once_per_entry(config, port_allocator, syslog_ng):
    network_source, file_destination = create_config(
        config, port_allocator,
        'aggregate(key=(input["key"]), values={"cnt": input["cnt"]}, timeout=1)',
    )
    syslog_ng.start(config)

    send_messages(
        network_source, [
            {"key": "host-a", "cnt": 1},
            {"key": "host-a", "cnt": 2},
        ],
    )
    read_results(file_destination, 2)

    # only the first message of the key should have armed a timer, so
    # exactly one ("timeout") replay is expected, not two
    results = read_results(file_destination, 1)
    assert results[0] == {"status": "timeout", "values": {"cnt": 3}}

    time.sleep(1.5)
    assert file_destination.get_stats()["processed"] == 3


def test_aggregators_argument_selects_replace_for_named_field_and_sum_for_others(config, port_allocator, syslog_ng):
    network_source, file_destination = create_config(
        config, port_allocator,
        'aggregate(key=(input["key"]), values={"total": input["amount"], "last": input["amount"]}, '
        'aggregators={"last": "replace"}, timeout=3600)',
    )
    syslog_ng.start(config)

    send_messages(
        network_source, [
            {"key": "host-a", "amount": 100},
            {"key": "host-a", "amount": 200},
        ],
    )

    results = read_results(file_destination, 2)
    assert results[0] == {"status": "absorbed", "values": {"total": 100, "last": 100}}
    assert results[1] == {"status": "absorbed", "values": {"total": 300, "last": 200}}


def test_aggregators_argument_defaults_unlisted_fields_to_sum(config, port_allocator, syslog_ng):
    network_source, file_destination = create_config(
        config, port_allocator,
        'aggregate(key=(input["key"]), values={"total": input["amount"], "last": input["amount"]}, '
        'aggregators={}, timeout=3600)',
    )
    syslog_ng.start(config)

    send_messages(
        network_source, [
            {"key": "host-a", "amount": 3},
            {"key": "host-a", "amount": 4},
        ],
    )

    results = read_results(file_destination, 2)
    assert results[1] == {"status": "absorbed", "values": {"total": 7, "last": 7}}


def test_aggregators_argument_rejects_unknown_function_name(config, port_allocator, syslog_ng):
    create_config(
        config, port_allocator,
        'aggregate(key=(input["key"]), values={"total": input["amount"]}, '
        'aggregators={"total": "no-such-function"}, timeout=3600)',
    )

    with pytest.raises(Exception):
        syslog_ng.start(config)


def test_aggregators_argument_supports_count_min_max_average(config, port_allocator, syslog_ng):
    network_source, file_destination = create_config(
        config, port_allocator,
        'aggregate(key=(input["key"]), '
        'values={"n": input["amount"], "lo": input["amount"], "hi": input["amount"], "avg": input["amount"]}, '
        'aggregators={"n": "count", "lo": "min", "hi": "max", "avg": "average"}, timeout=3600)',
    )
    syslog_ng.start(config)

    send_messages(
        network_source, [
            {"key": "host-a", "amount": 10},
            {"key": "host-a", "amount": 4},
            {"key": "host-a", "amount": 7},
        ],
    )

    results = read_results(file_destination, 3)
    assert results[0] == {"status": "absorbed", "values": {"n": 1, "lo": 10, "hi": 10, "avg": 10.0}}
    assert results[1] == {"status": "absorbed", "values": {"n": 2, "lo": 4, "hi": 10, "avg": 7.0}}
    assert results[2] == {"status": "absorbed", "values": {"n": 3, "lo": 4, "hi": 10, "avg": 7.0}}


def test_count_aggregator_does_not_count_null_values(config, port_allocator, syslog_ng):
    network_source, file_destination = create_config(
        config, port_allocator,
        'aggregate(key=(input["key"]), values={"n": input["amount"]}, aggregators={"n": "count"}, timeout=3600)',
    )
    syslog_ng.start(config)

    send_messages(
        network_source, [
            {"key": "host-a", "amount": 1},
            {"key": "host-a", "amount": None},
            {"key": "host-a", "amount": 2},
            {"key": "host-a", "amount": None},
        ],
    )

    results = read_results(file_destination, 4)
    assert results[0] == {"status": "absorbed", "values": {"n": 1}}
    assert results[1] == {"status": "absorbed", "values": {"n": 1}}
    assert results[2] == {"status": "absorbed", "values": {"n": 2}}
    assert results[3] == {"status": "absorbed", "values": {"n": 2}}


def test_aggregators_as_number_variants_coerce_string_values(config, port_allocator, syslog_ng):
    network_source, file_destination = create_config(
        config, port_allocator,
        'aggregate(key=(input["key"]), '
        'values={"total": input["amount"], "lo": input["amount"], "hi": input["amount"], "avg": input["amount"]}, '
        'aggregators={"total": "sum_as_number", "lo": "min_as_number", "hi": "max_as_number", '
        '"avg": "average_as_number"}, timeout=3600)',
    )
    syslog_ng.start(config)

    # "amount" arrives as a string, not a number: the plain sum/min/max/average
    # aggregators would fail to merge it past the first message
    send_messages(
        network_source, [
            {"key": "host-a", "amount": "5"},
            {"key": "host-a", "amount": "3"},
        ],
    )

    results = read_results(file_destination, 2)
    assert results[0] == {"status": "absorbed", "values": {"total": 5, "lo": 5, "hi": 5, "avg": 5.0}}
    assert results[1] == {"status": "absorbed", "values": {"total": 8, "lo": 3, "hi": 5, "avg": 4.0}}


def test_id_argument_survives_a_config_reload(config, port_allocator, syslog_ng):
    port = port_allocator()
    aggregate_call = 'aggregate(key=(input["key"]), values={"cnt": input["cnt"]}, id="reload-test", timeout=3600)'

    network_source, file_destination_before = create_config(config, port_allocator, aggregate_call, port=port)
    syslog_ng.start(config)

    send_messages(network_source, [{"key": "host-a", "cnt": 1}])
    # wait for the pre-reload message to be fully processed before reloading,
    # otherwise it could race with the reload and land in either generation
    results_before = read_results(file_destination_before, 1)
    assert results_before[0] == {"status": "absorbed", "values": {"cnt": 1}}

    # rebuild the exact same config (as a real reload normally would, e.g.
    # after editing something unrelated elsewhere in the file) and reload;
    # network_source is just a client-side socket helper bound to `port`,
    # still valid for sending after the reload since the port doesn't change
    _, file_destination_after = create_config(config, port_allocator, aggregate_call, port=port)
    syslog_ng.reload(config)

    send_messages(network_source, [{"key": "host-a", "cnt": 1}])

    # 1 (before reload) + 1 (after reload) == 2: the running total survived
    # the reload instead of starting over
    results_after = read_results(file_destination_after, 2)
    assert results_after[1] == {"status": "absorbed", "values": {"cnt": 2}}


def test_without_id_argument_state_does_not_survive_a_config_reload(config, port_allocator, syslog_ng):
    port = port_allocator()
    aggregate_call = 'aggregate(key=(input["key"]), values={"cnt": input["cnt"]}, timeout=3600)'

    network_source, file_destination_before = create_config(config, port_allocator, aggregate_call, port=port)
    syslog_ng.start(config)

    send_messages(network_source, [{"key": "host-a", "cnt": 1}])
    results_before = read_results(file_destination_before, 1)
    assert results_before[0] == {"status": "absorbed", "values": {"cnt": 1}}

    _, file_destination_after = create_config(config, port_allocator, aggregate_call, port=port)
    syslog_ng.reload(config)

    send_messages(network_source, [{"key": "host-a", "cnt": 1}])

    # starts fresh at 1, NOT 2 -- no id means no carry-over across a reload
    results_after = read_results(file_destination_after, 2)
    assert results_after[1] == {"status": "absorbed", "values": {"cnt": 1}}
