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
from pathlib import Path

import pytest
from axosyslog_light.common.blocking import wait_until_true
from axosyslog_light.common.file import File
from axosyslog_light.syslog_ng_config.statement_group import StatementGroup
from axosyslog_light.syslog_ng_ctl.prometheus_stats_handler import MetricFilter

NUM_MESSAGES = 10
SHAPES = ["nested-final", "if-else"]
OPEN_ATTEMPTS_FILE = "open-attempts.log"
SLOW_RESPONSE_SECONDS = 2
PYTHON_OUTPUT_FILE = "python-output.log"

PREAMBLE = r"""
python {
from syslogng import LogDestination


class UnreachableDestination(LogDestination):
    def init(self, options):
        self.path = options["path"]
        return True

    def open(self):
        with open(self.path, "a") as attempts:
            attempts.write("open attempt\n")
        return False

    def is_opened(self):
        return False

    def close(self):
        pass

    def send(self, msg):
        return True


class RecordingDestination(LogDestination):
    def init(self, options):
        self.path = options["path"]
        return True

    def open(self):
        return True

    def is_opened(self):
        return True

    def close(self):
        pass

    def send(self, msg):
        with open(self.path, "a") as output:
            output.write(msg["MSG"].decode() + "\n")
        return True
};
"""


def _messages(tag):
    return ["{} {:03d}".format(tag, i) for i in range(NUM_MESSAGES)]


def _syslog_lines(messages):
    return ["<13>Oct 11 22:14:15 host prog: {}".format(message) for message in messages]


def _python_destination(config, class_name, path):
    return config.create_python_destination(
        **{
            "class": config.stringify(class_name),
            "options": config.arrowed_options({"path": config.stringify(path)}),
            "time-reopen": 1,
        },
    )


def _read_lines(path, count):
    output_file = File(path)
    output_file.wait_for_creation()
    output_file.open("r")
    try:
        return [line.rstrip("\n") for line in output_file.wait_for_number_of_lines(count)]
    finally:
        output_file.close()


def _rejected_counts(config):
    samples = config.get_prometheus_samples([MetricFilter("syslogng_output_events_total", {"result": "rejected"})])
    return [int(sample.value) for sample in samples if sample.value]


def _delivered_count(config, driver):
    metric_filter = MetricFilter("syslogng_output_events_total", {"result": "delivered", "driver": driver})
    return sum(int(sample.value) for sample in config.get_prometheus_samples([metric_filter]))


class _NamedDestinationReference(StatementGroup):
    """destination(name); referencing a destination defined in the preamble"""

    def __init__(self, name):
        list.__init__(self)
        self.__name = name

    @property
    def group_type(self):
        return "destination"

    @property
    def group_id(self):
        return self.__name


def _create_failover_logpath(config, source, primary, secondary, shape="nested-final", failover=True):
    # nested-final:
    #   log {
    #       source(s_network);
    #       log {
    #           log { destination(primary); flags(final); };
    #           log { destination(secondary); flags(final); };
    #           flags(destination-failover);
    #       };
    #   };
    #
    # if-else:
    #   log {
    #       source(s_network);
    #       log {
    #           if { destination(primary); } else { destination(secondary); };
    #           flags(destination-failover);
    #       };
    #   };
    if shape == "nested-final":
        alternatives = [
            config.create_inner_logpath(statements=[primary], flags="final"),
            config.create_inner_logpath(statements=[secondary], flags="final"),
        ]
    else:
        alternatives = [config.create_logpath_if(if_elif_blocks=[[primary]], else_block=[secondary])]
    failover_logpath = config.create_inner_logpath(statements=alternatives, flags="destination-failover" if failover else None)
    config.create_logpath(statements=[source, failover_logpath])


@pytest.mark.parametrize("shape", SHAPES)
def test_destination_failover_network_fails_over_and_back(config, syslog_ng, port_allocator, shape):
    source = config.create_network_source(ip="localhost", port=port_allocator(), transport="tcp")
    primary = config.create_network_destination(ip="localhost", port=port_allocator(), transport="tcp", time_reopen=1)
    secondary = config.create_file_destination(file_name="secondary.log")
    _create_failover_logpath(config, source, primary, secondary, shape)

    syslog_ng.start(config)

    # Each failed connection attempt logs exactly one line carrying
    # time_reopen, right before the destination marks itself unreachable.
    # Seeing the second one means the first one's marking already happened.
    syslog_ng.wait_for_messages_in_console_log(["time_reopen=", "time_reopen="])

    failover_messages = _messages("failover")
    source.write_logs(_syslog_lines(failover_messages))
    assert len(secondary.read_until_logs(failover_messages)) == NUM_MESSAGES

    primary.start_listener()
    syslog_ng.wait_for_message_in_console_log("Syslog connection established")

    failback_messages = _messages("failback")
    source.write_logs(_syslog_lines(failback_messages))
    assert len(primary.read_until_logs(failback_messages)) == NUM_MESSAGES
    # only the messages sent while the primary was unreachable
    assert _rejected_counts(config) == [NUM_MESSAGES]

    syslog_ng.stop()
    assert not any(message in line for line in secondary.read_all() for message in failback_messages)


def test_destination_failover_threaded_destination_fails_over(config, syslog_ng, port_allocator):
    config.add_preamble(PREAMBLE)

    source = config.create_network_source(ip="localhost", port=port_allocator(), transport="tcp")
    primary = _python_destination(config, "UnreachableDestination", OPEN_ATTEMPTS_FILE)
    secondary = config.create_file_destination(file_name="secondary.log")
    _create_failover_logpath(config, source, primary, secondary)

    syslog_ng.start(config)

    # the worker marks itself unreachable right after a failed open(); the
    # second attempt proves the first one has been accounted for
    _read_lines(OPEN_ATTEMPTS_FILE, 2)

    messages = _messages("failover")
    source.write_logs(_syslog_lines(messages))
    assert len(secondary.read_until_logs(messages)) == NUM_MESSAGES
    assert _rejected_counts(config) == [NUM_MESSAGES]


def test_destination_failover_http_destination_stays_failed_over_while_its_retry_is_in_flight(config, syslog_ng, port_allocator):
    source = config.create_network_source(ip="localhost", port=port_allocator(), transport="tcp")
    # slow 503s, then 200s
    primary = config.create_http_destination(
        port=port_allocator(),
        responses=[(503, "", SLOW_RESPONSE_SECONDS)] * 3 + [(200, "")],
        time_reopen=1,
    )
    secondary = config.create_file_destination(file_name="secondary.log")
    _create_failover_logpath(config, source, primary, secondary)

    syslog_ng.start(config)

    # http() has no connect step, only a failed send marks it unreachable
    source.write_logs(_syslog_lines(["probe"]))
    # each 5XX is logged right before the marking, so the second one proves
    # the first was accounted for; the request after it is the retry in
    # flight, during which the worker used to count as reachable again
    syslog_ng.wait_for_messages_in_console_log(["Server returned with a 5XX", "Server returned with a 5XX"])
    syslog_ng.wait_for_message_in_console_log("http: Sending HTTP request")

    failover_messages = _messages("failover")
    source.write_logs(_syslog_lines(failover_messages))
    assert len(secondary.read_until_logs(failover_messages)) == NUM_MESSAGES
    assert _rejected_counts(config) == [NUM_MESSAGES]

    # the probe's delivery makes the primary reachable again
    assert wait_until_true(lambda: _delivered_count(config, "http") == 1)

    failback_messages = _messages("failback")
    source.write_logs(_syslog_lines(failback_messages))
    # the server saw the probe's three failed attempts and its delivery, and nothing else before the failback
    assert primary.read_until_logs(failback_messages) == ["probe"] * 4 + failback_messages
    assert _rejected_counts(config) == [NUM_MESSAGES]

    syslog_ng.stop()
    assert not any(message in line for line in secondary.read_all() for message in failback_messages)


def test_destination_failover_file_destination_fails_over(config, syslog_ng, port_allocator):
    # file() registers its counters from stats(level(1))
    config.update_global_options(stats_level=1)
    source = config.create_network_source(ip="localhost", port=port_allocator(), transport="tcp")
    # no create-dirs(), so the file cannot be opened
    primary = config.create_file_destination(file_name="missing-dir/primary.log")
    secondary = config.create_file_destination(file_name="secondary.log")
    _create_failover_logpath(config, source, primary, secondary)

    syslog_ng.start(config)

    messages = _messages("failover")
    source.write_logs(_syslog_lines(messages))
    assert len(secondary.read_until_logs(messages)) == NUM_MESSAGES
    assert _rejected_counts(config) == [NUM_MESSAGES]

    syslog_ng.stop()
    assert not Path("missing-dir").exists()


def test_destination_failover_parallel_branch_without_destination_still_fails_over(config, syslog_ng, port_allocator):
    # log { log { filterx { true; }; }; log { destination(primary); }; }: the
    # matching branch delivers the message nowhere, it must not stop the failover
    source = config.create_network_source(ip="localhost", port=port_allocator(), transport="tcp")
    primary = config.create_inner_logpath(
        statements=[
            config.create_inner_logpath(statements=[config.create_filterx("true;")]),
            config.create_inner_logpath(statements=[config.create_file_destination(file_name="missing-dir/primary.log")]),
        ],
    )
    secondary = config.create_file_destination(file_name="secondary.log")
    _create_failover_logpath(config, source, primary, secondary)

    syslog_ng.start(config)

    messages = _messages("failover")
    source.write_logs(_syslog_lines(messages))
    assert len(secondary.read_until_logs(messages)) == NUM_MESSAGES

    syslog_ng.stop()
    assert not Path("missing-dir").exists()


def test_destination_failover_file_destination_is_unreachable_per_file(config, syslog_ng, port_allocator):
    Path("dir-good").mkdir()

    source = config.create_network_source(ip="localhost", port=port_allocator(), transport="tcp")
    primary = config.create_file_destination(file_name="'dir-${PROGRAM}/primary.log'")
    secondary = config.create_file_destination(file_name="secondary.log")
    _create_failover_logpath(config, source, primary, secondary)

    syslog_ng.start(config)

    good_messages = _messages("good")
    bad_messages = _messages("bad")
    good_lines = ["<13>Oct 11 22:14:15 host good: {}".format(message) for message in good_messages]
    bad_lines = ["<13>Oct 11 22:14:15 host bad: {}".format(message) for message in bad_messages]
    source.write_logs(good_lines + bad_lines)
    assert len(secondary.read_until_logs(bad_messages)) == NUM_MESSAGES
    assert len(_read_lines("dir-good/primary.log", NUM_MESSAGES)) == NUM_MESSAGES

    syslog_ng.stop()
    assert not any(message in line for line in secondary.read_all() for message in good_messages)


def test_destination_failover_drop_inside_destination_reference_fails_over(config, syslog_ng, port_allocator):
    config.add_preamble(
        r"""
destination d_dropping {
    channel {
        filterx { false; };
        destination { file("dropped.log"); };
    };
};
""",
    )

    source = config.create_network_source(ip="localhost", port=port_allocator(), transport="tcp")
    primary = _NamedDestinationReference("d_dropping")
    secondary = config.create_file_destination(file_name="secondary.log")
    _create_failover_logpath(config, source, primary, secondary)

    syslog_ng.start(config)

    messages = _messages("failover")
    source.write_logs(_syslog_lines(messages))
    assert len(secondary.read_until_logs(messages)) == NUM_MESSAGES

    syslog_ng.stop()
    assert not Path("dropped.log").exists()


DROP_FILTERX = "if ($MSG =~ '^dropped') { drop; };"


def test_destination_failover_drop_in_the_alternative_does_not_fail_over(config, syslog_ng, port_allocator):
    # log {
    #     source(s_network);
    #     log {
    #         log { filterx { if (...) { drop; }; }; destination(primary); flags(final); };
    #         log { destination(secondary); flags(final); };
    #         flags(destination-failover);
    #     };
    # };
    #
    # drop discards the message on purpose, unlike a filter that does not
    # match: the alternative is not tried again elsewhere
    source = config.create_network_source(ip="localhost", port=port_allocator(), transport="tcp")
    primary = config.create_file_destination(file_name="primary.log")
    secondary = config.create_file_destination(file_name="secondary.log")
    alternatives = [
        config.create_inner_logpath(statements=[config.create_filterx(DROP_FILTERX), primary], flags="final"),
        config.create_inner_logpath(statements=[secondary], flags="final"),
    ]
    failover_logpath = config.create_inner_logpath(statements=alternatives, flags="destination-failover")
    config.create_logpath(statements=[source, failover_logpath])

    syslog_ng.start(config)

    # the message after the dropped ones proves they have been processed
    source.write_logs(_syslog_lines(_messages("dropped") + ["sync"]))
    assert _read_lines("primary.log", 1)[-1].endswith("sync")

    syslog_ng.stop()
    assert not Path("secondary.log").exists()


def test_destination_failover_drop_inside_destination_reference_accepts_the_message(config, syslog_ng, port_allocator):
    config.add_preamble(
        r"""
destination d_dropping {
    channel {
        filterx { %s };
        destination { file("primary.log"); };
    };
};
""" % DROP_FILTERX,
    )

    # log {
    #     source(s_network);
    #     log {
    #         log { destination(d_dropping); destination(tap); flags(final); };
    #         log { destination(secondary); flags(final); };
    #         flags(destination-failover);
    #     };
    # };
    #
    # the drop inside d_dropping counts as a delivery: the unreachable tap
    # chained after it buffers instead of rejecting, nothing fails over
    source = config.create_network_source(ip="localhost", port=port_allocator(), transport="tcp")
    tap = config.create_file_destination(file_name="missing-dir/tap.log")
    primary = config.create_inner_logpath(statements=[_NamedDestinationReference("d_dropping"), tap])
    secondary = config.create_file_destination(file_name="secondary.log")
    _create_failover_logpath(config, source, primary, secondary)

    syslog_ng.start(config)

    source.write_logs(_syslog_lines(_messages("dropped") + ["sync"]))
    assert _read_lines("primary.log", 1)[-1].endswith("sync")
    assert _rejected_counts(config) == []

    syslog_ng.stop()
    assert not Path("secondary.log").exists()


@pytest.mark.parametrize("shape", SHAPES)
def test_destination_failover_reachable_primary_takes_everything(config, syslog_ng, port_allocator, shape):
    config.add_preamble(PREAMBLE)

    source = config.create_network_source(ip="localhost", port=port_allocator(), transport="tcp")
    primary = _python_destination(config, "RecordingDestination", PYTHON_OUTPUT_FILE)
    secondary = config.create_file_destination(file_name="secondary.log")
    _create_failover_logpath(config, source, primary, secondary, shape)

    syslog_ng.start(config)

    messages = _messages("primary")
    source.write_logs(_syslog_lines(messages))
    assert _read_lines(PYTHON_OUTPUT_FILE, NUM_MESSAGES) == messages

    syslog_ng.stop()
    assert not Path("secondary.log").exists()


def test_unreachable_destination_without_destination_failover_does_not_fail_over(config, syslog_ng, port_allocator):
    source = config.create_network_source(ip="localhost", port=port_allocator(), transport="tcp")
    primary = config.create_file_destination(file_name="missing-dir/primary.log")
    secondary = config.create_file_destination(file_name="secondary.log")
    _create_failover_logpath(config, source, primary, secondary, failover=False)

    # a later sibling path: once a message shows up here, the routing
    # decision in the failover-less path before it has already been made
    marker = config.create_file_destination(file_name="marker.log")
    config.create_logpath(statements=[source, marker])

    syslog_ng.start(config)

    messages = _messages("buffered")
    source.write_logs(_syslog_lines(messages))
    assert len(marker.read_until_logs(messages)) == NUM_MESSAGES

    syslog_ng.stop()
    assert not Path("secondary.log").exists()


def test_destination_failover_last_alternative_buffers_when_everything_is_down(config, syslog_ng, port_allocator):
    source = config.create_network_source(ip="localhost", port=port_allocator(), transport="tcp")
    primary = config.create_file_destination(file_name="missing-dir/primary.log")
    secondary = config.create_network_destination(ip="localhost", port=port_allocator(), transport="tcp", time_reopen=1)
    _create_failover_logpath(config, source, primary, secondary)

    syslog_ng.start(config)

    messages = _messages("buffered")
    source.write_logs(_syslog_lines(messages))

    # the secondary is the last alternative, so it buffers instead of
    # rejecting, and delivers once it becomes reachable
    secondary.start_listener()
    assert len(secondary.read_until_logs(messages)) == NUM_MESSAGES


def test_destination_failover_elif_chain_fails_over_to_the_else_branch(config, syslog_ng, port_allocator):
    # log {
    #     source(s_network);
    #     log {
    #         if { destination(primary); } elif { destination(secondary); } else { destination(tertiary); };
    #         flags(destination-failover);
    #     };
    # };
    source = config.create_network_source(ip="localhost", port=port_allocator(), transport="tcp")
    primary = config.create_file_destination(file_name="missing-dir/primary.log")
    secondary = config.create_file_destination(file_name="missing-dir2/secondary.log")
    tertiary = config.create_file_destination(file_name="tertiary.log")
    alternatives = config.create_logpath_if(if_elif_blocks=[[primary], [secondary]], else_block=[tertiary])
    failover_logpath = config.create_inner_logpath(statements=[alternatives], flags="destination-failover")
    config.create_logpath(statements=[source, failover_logpath])

    syslog_ng.start(config)

    messages = _messages("failover")
    source.write_logs(_syslog_lines(messages))
    assert len(tertiary.read_until_logs(messages)) == NUM_MESSAGES


@pytest.mark.parametrize("inner_secondary_reachable", [True, False], ids=["inner-secondary-up", "inner-down"])
def test_destination_failover_nested_failover_junction(config, syslog_ng, port_allocator, inner_secondary_reachable):
    # log {
    #     source(s_network);
    #     log {
    #         log {
    #             log { destination(inner_primary); flags(final); };
    #             log { destination(inner_secondary); flags(final); };
    #             flags(destination-failover, final);
    #         };
    #         log { destination(outer_secondary); flags(final); };
    #         flags(destination-failover);
    #     };
    # };
    #
    # inner_primary is always down.  If inner_secondary is up, the inner
    # junction delivers and the outer one must not fail over.  If it is down
    # too, it rejects instead of buffering, as the outer junction still has
    # an alternative to try.
    source = config.create_network_source(ip="localhost", port=port_allocator(), transport="tcp")
    inner_primary = config.create_file_destination(file_name="missing-dir/inner-primary.log")
    inner_secondary = config.create_file_destination(
        file_name="inner-secondary.log" if inner_secondary_reachable else "missing-dir2/inner-secondary.log",
    )
    outer_secondary = config.create_file_destination(file_name="outer-secondary.log")
    inner_failover_logpath = config.create_inner_logpath(
        statements=[
            config.create_inner_logpath(statements=[inner_primary], flags="final"),
            config.create_inner_logpath(statements=[inner_secondary], flags="final"),
        ],
        flags="destination-failover, final",
    )
    outer_failover_logpath = config.create_inner_logpath(
        statements=[
            inner_failover_logpath,
            config.create_inner_logpath(statements=[outer_secondary], flags="final"),
        ],
        flags="destination-failover",
    )
    config.create_logpath(statements=[source, outer_failover_logpath])

    syslog_ng.start(config)

    messages = _messages("nested")
    source.write_logs(_syslog_lines(messages))
    if inner_secondary_reachable:
        assert len(inner_secondary.read_until_logs(messages)) == NUM_MESSAGES
    else:
        assert len(outer_secondary.read_until_logs(messages)) == NUM_MESSAGES

    syslog_ng.stop()
    assert not Path("missing-dir").exists()
    if inner_secondary_reachable:
        assert not Path("outer-secondary.log").exists()
    else:
        assert not Path("missing-dir2").exists()


def test_destination_failover_fallback_branch_is_the_last_alternative(config, syslog_ng, port_allocator):
    # log {
    #     source(s_network);
    #     log {
    #         log { destination(fallback); flags(fallback); };
    #         log { destination(primary); flags(final); };
    #         log { destination(secondary); flags(final); };
    #         flags(destination-failover);
    #     };
    # };
    source = config.create_network_source(ip="localhost", port=port_allocator(), transport="tcp")
    fallback = config.create_file_destination(file_name="fallback.log")
    primary = config.create_file_destination(file_name="missing-dir/primary.log")
    secondary = config.create_file_destination(file_name="missing-dir2/secondary.log")
    alternatives = [
        config.create_inner_logpath(statements=[fallback], flags="fallback"),
        config.create_inner_logpath(statements=[primary], flags="final"),
        config.create_inner_logpath(statements=[secondary], flags="final"),
    ]
    failover_logpath = config.create_inner_logpath(statements=alternatives, flags="destination-failover")
    config.create_logpath(statements=[source, failover_logpath])

    syslog_ng.start(config)

    messages = _messages("failover")
    source.write_logs(_syslog_lines(messages))
    assert len(fallback.read_until_logs(messages)) == NUM_MESSAGES


def test_destination_failover_rejected_message_skips_the_rest_of_the_destination_reference(config, syslog_ng, port_allocator):
    config.add_preamble(
        r"""
destination d_unreachable_then_metric {
    channel {
        destination { file("missing-dir/primary.log"); };
        filterx { update_metric("primary_accepted_total"); };
        destination { file("after-primary.log"); };
    };
};

destination d_secondary_then_metric {
    channel {
        destination { file("secondary.log"); };
        filterx { update_metric("secondary_accepted_total"); };
    };
};
""",
    )

    source = config.create_network_source(ip="localhost", port=port_allocator(), transport="tcp")
    primary = _NamedDestinationReference("d_unreachable_then_metric")
    secondary = _NamedDestinationReference("d_secondary_then_metric")
    _create_failover_logpath(config, source, primary, secondary)

    syslog_ng.start(config)

    messages = _messages("failover")
    source.write_logs(_syslog_lines(messages))
    assert _read_lines("secondary.log", NUM_MESSAGES)[-1].endswith(messages[-1])

    samples = config.get_prometheus_samples([MetricFilter("syslogng_secondary_accepted_total", {})])
    assert [int(sample.value) for sample in samples] == [NUM_MESSAGES]
    samples = config.get_prometheus_samples([MetricFilter("syslogng_primary_accepted_total", {})])
    assert all(int(sample.value) == 0 for sample in samples)

    syslog_ng.stop()
    assert not Path("after-primary.log").exists()


def _assert_primary_delivers_without_failover(config, syslog_ng, port_allocator, primary_body):
    config.add_preamble(
        r"""
destination d_primary {
    channel {
        %s
        filterx { update_metric("primary_accepted_total"); };
    };
};
""" % primary_body,
    )

    source = config.create_network_source(ip="localhost", port=port_allocator(), transport="tcp")
    primary = _NamedDestinationReference("d_primary")
    secondary = config.create_file_destination(file_name="secondary.log")
    _create_failover_logpath(config, source, primary, secondary)

    syslog_ng.start(config)

    messages = _messages("primary")
    source.write_logs(_syslog_lines(messages))
    assert _read_lines("primary.log", NUM_MESSAGES)[-1].endswith(messages[-1])

    samples = config.get_prometheus_samples([MetricFilter("syslogng_primary_accepted_total", {})])
    assert [int(sample.value) for sample in samples] == [NUM_MESSAGES]

    syslog_ng.stop()
    assert not Path("secondary.log").exists()


def test_destination_failover_destination_chained_after_a_successful_one_does_not_fail_over(config, syslog_ng, port_allocator):
    # the unreachable tap follows a destination that already accepted the
    # message: it buffers instead of rejecting, no duplicate in secondary
    _assert_primary_delivers_without_failover(
        config, syslog_ng, port_allocator,
        r"""
        destination { file("primary.log"); };
        destination { file("missing-dir/tap.log"); };
        """,
    )


def test_destination_failover_drivers_of_a_destination_block_are_equivalent(config, syslog_ng, port_allocator):
    # one of the drivers of the same destination block accepting the
    # message means it is delivered, the failover happens above it
    _assert_primary_delivers_without_failover(
        config, syslog_ng, port_allocator,
        r"""
        destination { file("missing-dir/unreachable.log"); file("primary.log"); };
        """,
    )
