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
from axosyslog_light.common.file import File
from axosyslog_light.syslog_ng_config.statement_group import StatementGroup

NUM_MESSAGES = 10
SHAPES = ["nested-final", "if-else"]
OPEN_ATTEMPTS_FILE = "open-attempts.log"
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


def test_destination_failover_file_destination_fails_over(config, syslog_ng, port_allocator):
    source = config.create_network_source(ip="localhost", port=port_allocator(), transport="tcp")
    # no create-dirs(), so the file cannot be opened
    primary = config.create_file_destination(file_name="missing-dir/primary.log")
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
