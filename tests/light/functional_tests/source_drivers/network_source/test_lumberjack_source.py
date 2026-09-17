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
"""End-to-end tests of the Lumberjack receiver: `transport(lumberjack(...))` of
the network() source driver.

The sender is axosyslog_light.driver_io.network.lumberjack_sender, which
speaks the protocol on a raw socket, so a test sees the exact frames and
acknowledgements.  Section numbers refer to the Lumberjack protocol
specification.
"""
import typing

from axosyslog_light.common.file import copy_shared_file
from axosyslog_light.driver_io.network.lumberjack_sender import LumberjackSender
from axosyslog_light.syslog_ng_ctl.prometheus_stats_handler import MetricFilter

# the protocol version the frame arrived in, then the payload as it was sent
MESSAGE_TEMPLATE = r'"${.lumberjack.version} ${MESSAGE}\n"'


def events(count: int, first: int = 0) -> typing.List[dict]:
    return [{"message": "message-{}".format(index), "seq": index} for index in range(first, first + count)]


def expected_messages(count: int, first: int = 0, version: int = 2) -> typing.List[str]:
    return [
        '{} {{"message":"message-{}","seq":{}}}'.format(version, index, index)
        for index in range(first, first + count)
    ]


def tls_options(testcase_parameters) -> dict:
    return {
        "key-file": copy_shared_file(testcase_parameters, "server.key"),
        "cert-file": copy_shared_file(testcase_parameters, "server.crt"),
        "peer-verify": "optional-untrusted",
    }


def start_lumberjack_receiver(config, syslog_ng, port, transport="lumberjack", **source_options):
    """Build a one-source, one-file-destination configuration and start it."""
    source = config.create_network_source(ip="localhost", port=port, transport=transport, **source_options)
    destination = config.create_file_destination(file_name="output.txt", template=MESSAGE_TEMPLATE)
    config.create_logpath(statements=[source, destination])

    syslog_ng.start(config)

    return destination


def test_lumberjack_delivers_a_window(config, syslog_ng, port_allocator):
    """The payload of every frame is $MESSAGE as sent, and the window is acknowledged as a whole (spec 7, 8)."""
    port = port_allocator()
    destination = start_lumberjack_receiver(config, syslog_ng, port)

    with LumberjackSender() as sender:
        sender.connect("localhost", port)
        assert sender.send_batch(events(3)) == 3

    assert destination.read_logs(3) == expected_messages(3)


def test_lumberjack_pipelined_windows_are_acknowledged_in_order(config, syslog_ng, port_allocator):
    """A sender that does not wait for the ACK gets one ACK per window, in order (spec 11, Appendix C.3)."""
    port = port_allocator()
    destination = start_lumberjack_receiver(config, syslog_ng, port)

    with LumberjackSender() as sender:
        sender.connect("localhost", port)
        sender.send_window(events(2))
        sender.send_window(events(3, first=2))
        assert sender.read_ack() == 2
        assert sender.read_ack() == 3

    assert destination.read_logs(5) == expected_messages(5)


def test_lumberjack_running_sequence_counter_is_echoed(config, syslog_ng, port_allocator):
    """A sender numbering frames from a counter running across windows gets its last sequence back (spec 8.3)."""
    port = port_allocator()
    destination = start_lumberjack_receiver(config, syslog_ng, port)

    with LumberjackSender() as sender:
        sender.connect("localhost", port)
        assert sender.send_batch(events(2), first_seq=1) == 2
        assert sender.send_batch(events(3, first=2), first_seq=3) == 5

    assert destination.read_logs(5) == expected_messages(5)


def test_lumberjack_version1_data_frames_become_json(config, syslog_ng, port_allocator):
    """The key/value pairs of a version 1 frame arrive as a flat JSON object with string values (spec 5.2)."""
    port = port_allocator()
    destination = start_lumberjack_receiver(config, syslog_ng, port)

    with LumberjackSender(version=1) as sender:
        sender.connect("localhost", port)
        assert sender.send_batch([
            [("line", "hello world"), ("host", "web-1"), ("offset", "42")],
            [("line", "second"), ("line", "duplicate keys stay")],
        ]) == 2

    assert destination.read_logs(2) == [
        '1 {"line":"hello world","host":"web-1","offset":"42"}',
        '1 {"line":"second","line":"duplicate keys stay"}',
    ]


def test_lumberjack_compressed_window_is_inflated(config, syslog_ng, port_allocator):
    """A window sent as one `C` frame, the Beats default at level 3, is inflated and acknowledged (spec 9)."""
    port = port_allocator()
    destination = start_lumberjack_receiver(config, syslog_ng, port)

    with LumberjackSender(compression_level=3) as sender:
        sender.connect("localhost", port)
        assert sender.send_batch(events(3)) == 3

    assert destination.read_logs(3) == expected_messages(3)


def test_lumberjack_compressed_version1_window(config, syslog_ng, port_allocator):
    """logstash-forwarder always compressed: a version 1 window is `W` and one `C` frame of `D` frames."""
    port = port_allocator()
    destination = start_lumberjack_receiver(config, syslog_ng, port)

    with LumberjackSender(version=1, compression_level=3) as sender:
        sender.connect("localhost", port)
        assert sender.send_batch([[("line", "compressed"), ("host", "web-1")]]) == 1

    assert destination.read_logs(1) == ['1 {"line":"compressed","host":"web-1"}']


def test_lumberjack_compressed_and_plain_windows_share_a_connection(config, syslog_ng, port_allocator):
    """A receiver accepts plain and compressed windows interchangeably on one connection (spec 9.3)."""
    port = port_allocator()
    destination = start_lumberjack_receiver(config, syslog_ng, port)

    with LumberjackSender(compression_level=6) as sender:
        sender.connect("localhost", port)
        assert sender.send_batch(events(2)) == 2
        sender.compression_level = 0
        assert sender.send_batch(events(2, first=2)) == 2
        sender.compression_level = 1
        assert sender.send_batch(events(1, first=4)) == 1

    assert destination.read_logs(5) == expected_messages(5)


def test_lumberjack_corrupt_compressed_frame_closes_the_connection(config, syslog_ng, port_allocator):
    """A `C` frame that is not a zlib stream is a protocol error: no ACK, the connection closes (spec 14.1)."""
    port = port_allocator()
    start_lumberjack_receiver(config, syslog_ng, port)

    with LumberjackSender() as sender:
        sender.connect("localhost", port)
        sender.send_raw(sender.encode_window(2, 1) + b"2C" + b"\x00\x00\x00\x05" + b"\x00" * 5)
        sender.wait_for_close()


def test_lumberjack_over_tls(config, syslog_ng, port_allocator, testcase_parameters):
    """A tls() block on the driver means TLS from the first octet, no STARTTLS (spec 15.2)."""
    port = port_allocator()
    destination = start_lumberjack_receiver(config, syslog_ng, port, tls=tls_options(testcase_parameters))

    with LumberjackSender() as sender:
        sender.connect("localhost", port, tls=True)
        assert sender.send_batch(events(3)) == 3

    assert destination.read_logs(3) == expected_messages(3)


def test_lumberjack_window_above_the_limit_closes_the_connection(config, syslog_ng, port_allocator):
    """A window above max-window-size() is a protocol error: no ACK, the connection closes (spec 16)."""
    port = port_allocator()
    destination = start_lumberjack_receiver(config, syslog_ng, port, transport="lumberjack(max-window-size(2))")

    with LumberjackSender() as sender:
        sender.connect("localhost", port)
        sender.send_window(events(3))
        sender.wait_for_close()

    # the sender reconnects with a window the receiver accepts
    with LumberjackSender() as sender:
        sender.connect("localhost", port)
        assert sender.send_batch(events(2)) == 2

    assert destination.read_logs(2) == expected_messages(2)


def test_lumberjack_frame_outside_a_window_closes_the_connection(config, syslog_ng, port_allocator):
    """A data frame with no window announcing it is a protocol error (spec 14.1)."""
    port = port_allocator()
    start_lumberjack_receiver(config, syslog_ng, port)

    with LumberjackSender() as sender:
        sender.connect("localhost", port)
        sender.send_raw(sender.encode_json_frame(1, b'{"orphan":true}'))
        sender.wait_for_close()


def test_lumberjack_metrics(config, syslog_ng, port_allocator):
    """Frames by version and acknowledgements are counted at stats level 1."""
    config.update_global_options(stats_level=1)
    port = port_allocator()
    destination = start_lumberjack_receiver(config, syslog_ng, port)

    with LumberjackSender() as sender:
        sender.connect("localhost", port)
        assert sender.send_batch(events(3)) == 3
        assert sender.send_batch(events(1, first=3)) == 1
    assert destination.read_logs(4) == expected_messages(4)

    frames = config.get_prometheus_samples([MetricFilter("syslogng_lumberjack_frames_total", {"version": "2"})])
    assert sum(sample.value for sample in frames) == 4
    acks = config.get_prometheus_samples([MetricFilter("syslogng_lumberjack_acknowledgements_total", {})])
    assert sum(sample.value for sample in acks) == 2
