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
"""End-to-end tests of the ALTP Receiver: `transport(altp(...))` of the
network() and syslog() source drivers.

The Sender is axosyslog_light.driver_io.network.altp_sender, which speaks the
protocol on a raw socket, so a test sees the exact commands and replies.
"""
import secrets
import socket
import typing

import pytest
from axosyslog_light.common.file import copy_shared_file
from axosyslog_light.driver_io.network.altp_sender import AltpError
from axosyslog_light.driver_io.network.altp_sender import AltpSender

MESSAGE_TEMPLATE = r'"${MESSAGE}\n"'


def new_session_id() -> str:
    """A Session ID a Sender SHOULD generate: 16 random octets in lowercase hex (spec 7.1)."""
    return secrets.token_hex(16)


def rfc3164_payloads(count: int, first: int = 0) -> typing.List[bytes]:
    return [
        "<13>Sep  3 10:00:00 localhost altp: message-{}".format(index).encode("utf-8")
        for index in range(first, first + count)
    ]


def stuck_payloads(count: int, first: int = 0) -> typing.List[bytes]:
    """Payloads whose message text matches the filter of the destination that is down."""
    return [
        "<13>Sep  3 10:00:00 localhost altp: stuck-{}".format(index).encode("utf-8")
        for index in range(first, first + count)
    ]


def expected_stuck_messages(count: int, first: int = 0) -> typing.List[str]:
    return ["stuck-{}".format(index) for index in range(first, first + count)]


def rfc5424_payloads(count: int, first: int = 0) -> typing.List[bytes]:
    return [
        "<34>1 2026-09-03T10:00:00.000Z localhost altp - - - message-{}".format(index).encode("utf-8")
        for index in range(first, first + count)
    ]


def expected_messages(count: int, first: int = 0) -> typing.List[str]:
    return ["message-{}".format(index) for index in range(first, first + count)]


def tls_options(testcase_parameters) -> dict:
    return {
        "key-file": copy_shared_file(testcase_parameters, "server.key"),
        "cert-file": copy_shared_file(testcase_parameters, "server.crt"),
        "peer-verify": "optional-untrusted",
    }


def start_altp_receiver(config, syslog_ng, port, driver="network", transport="altp", **source_options):
    """Build a one-source, one-file-destination configuration and start it."""
    create_source = config.create_network_source if driver == "network" else config.create_syslog_source
    source = create_source(ip="localhost", port=port, transport=transport, **source_options)
    destination = config.create_file_destination(file_name="output.txt", template=MESSAGE_TEMPLATE)
    config.create_logpath(statements=[source, destination])

    syslog_ng.start(config)

    return destination


def test_altp_delivers_batch(config, syslog_ng, port_allocator):
    """The minimal plaintext session of Appendix A.1."""
    port = port_allocator()
    destination = start_altp_receiver(config, syslog_ng, port)
    session_id = new_session_id()

    with AltpSender() as sender:
        assert sender.connect("localhost", port) == "ALTP 1.0"
        # no tls(), so the Capability list is the single line `250 \n` (spec 5.3)
        assert sender.ehlo() == []
        assert sender.sync(session_id) == 0
        assert sender.send_batch(rfc3164_payloads(3)) == 3
        sender.noop()

    assert destination.read_logs(3) == expected_messages(3)


def test_altp_counters_survive_restart(config, syslog_ng, port_allocator):
    """A Sender that reconnects resumes from the counters of its Session (spec 10.3, Appendix A.3)."""
    port = port_allocator()
    destination = start_altp_receiver(config, syslog_ng, port)
    session_id = new_session_id()

    with AltpSender() as sender:
        sender.connect("localhost", port)
        sender.ehlo()
        assert sender.sync(session_id) == 0
        assert sender.send_batch(rfc3164_payloads(3)) == 3
        # the counters are reset by the `DATA` command line of the next Batch,
        # so this one is acknowledged with its own count, not a cumulative one (spec 9.2)
        assert sender.send_batch(rfc3164_payloads(2, first=3)) == 2
        # dropping the Connection here simulates an acknowledgement lost in flight

    with AltpSender() as sender:
        sender.connect("localhost", port)
        sender.ehlo()
        # the counters the previous Connection left un-reset (spec 9.4, step 1)
        assert sender.sync(session_id) == 2
        # that SYNC reply is itself an acknowledgement, so the reset falls due
        # on this Connection's next command line (ADR-0004, spec 10.1)
        sender.noop()
        assert sender.sync(session_id) == 0

    assert destination.read_logs(5) == expected_messages(5)


def test_altp_counters_survive_syslog_ng_restart(config, syslog_ng, port_allocator):
    """A restarted Receiver reports the durable prefix it persisted (spec 10.1)."""
    port = port_allocator()
    destination = start_altp_receiver(config, syslog_ng, port)
    session_id = new_session_id()

    with AltpSender() as sender:
        sender.connect("localhost", port)
        sender.ehlo()
        assert sender.sync(session_id) == 0
        assert sender.send_batch(rfc3164_payloads(3)) == 3

    assert destination.read_logs(3) == expected_messages(3)

    syslog_ng.restart(config)

    with AltpSender() as sender:
        sender.connect("localhost", port)
        sender.ehlo()
        # the restart rule frames_read := frames_acked (spec 10.1) leaves the
        # count at 3, because the Batch was acknowledged in full
        assert sender.sync(session_id) == 3
        sender.noop()
        assert sender.send_batch(rfc3164_payloads(2, first=3)) == 2

    # read_logs() continues where the previous read left off
    assert destination.read_logs(2) == expected_messages(2, first=3)


def test_altp_interrupted_batch_survives_syslog_ng_restart(config, syslog_ng, port_allocator):
    """Frames of an interrupted Batch that became durable stay counted (spec 8.5, 10.1)."""
    port = port_allocator()
    destination = start_altp_receiver(config, syslog_ng, port)
    session_id = new_session_id()

    with AltpSender() as sender:
        sender.connect("localhost", port)
        sender.ehlo()
        assert sender.sync(session_id) == 0
        sender.open_batch()
        for payload in rfc3164_payloads(2):
            sender.send_frame(payload)
        # no terminator and no acknowledgement: the close aborts the Batch and
        # the counters are kept as they stand (spec 8.5)

    # a file destination acknowledges a message once it is written, so reading
    # both back proves frames_acked is 2 and the restart rule keeps it there
    assert destination.read_logs(2) == expected_messages(2)

    syslog_ng.restart(config)

    with AltpSender() as sender:
        sender.connect("localhost", port)
        sender.ehlo()
        assert sender.sync(session_id) == 2


def test_altp_abandons_the_batch_on_the_acknowledgement_timeout(config, syslog_ng, port_allocator):
    """The acknowledgement timeout expires and the Receiver abandons the Batch (spec 9.3, 12.1, ADR-0009).

    Three of the five frames become durable in a file destination, the other two
    stay in the pipeline behind a network() destination with nothing listening.
    `ack-timeout-action(close)` answers the expiry with `421 Try again later` and
    closes without acknowledging anything, so nothing is disowned or resent.
    """
    altp_port = port_allocator()
    stuck_port = port_allocator()

    source = config.create_network_source(ip="localhost", port=altp_port, transport="altp(ack-timeout(2))")
    stuck_destination = config.create_network_destination(
        ip="localhost",
        port=stuck_port,
        time_reopen=1,
        template=MESSAGE_TEMPLATE,
    )
    durable_destination = config.create_file_destination(file_name="output.txt", template=MESSAGE_TEMPLATE)
    stuck_filter = config.create_match_filter(match_string=config.stringify("stuck"), value=config.stringify("MESSAGE"))

    config.create_logpath(statements=[source, stuck_filter, stuck_destination], flags="final flow-control")
    config.create_logpath(statements=[source, durable_destination], flags="flow-control")

    syslog_ng.start(config)
    session_id = new_session_id()

    with AltpSender() as sender:
        sender.connect("localhost", altp_port)
        sender.ehlo()
        assert sender.sync(session_id) == 0
        sender.open_batch()
        for payload in rfc3164_payloads(3) + stuck_payloads(2):
            sender.send_frame(payload)
        # send_batch() parses a count out of the reply, and this Batch draws no
        # acknowledgement at all, so the terminator is written by hand
        sender.send_command(".")
        assert sender.read_reply(timeout=30) == (421, ["Try again later"])
        sender.wait_for_close()

    assert durable_destination.read_logs(3) == expected_messages(3)

    with AltpSender() as sender:
        sender.connect("localhost", altp_port)
        sender.ehlo()
        # `frames_read > frames_acked`: two frames of the abandoned Batch are
        # still in the pipeline, so the reply is deferred (spec 7.2)
        sender.send_command("SYNC {}".format(session_id))
        with pytest.raises(socket.timeout):
            sender.read_reply(timeout=1)

        stuck_destination.start_listener()

        # the count covers every frame: the Receiver never disowned the two it held
        assert sender.read_reply(timeout=30) == (250, ["Received 5"])

    # exactly once: the abandoned Batch was never partially acknowledged, so
    # neither message was resent
    stuck_messages = [message.rstrip("\n") for message in stuck_destination.read_logs(2)]
    assert stuck_messages == expected_stuck_messages(2)


def test_altp_newest_connection_wins(config, syslog_ng, port_allocator):
    """A SYNC for a Session held by a live Connection displaces the older one (spec 7.3)."""
    port = port_allocator()
    destination = start_altp_receiver(config, syslog_ng, port)
    session_id = new_session_id()

    with AltpSender() as older, AltpSender() as newer:
        older.connect("localhost", port)
        older.ehlo()
        assert older.sync(session_id) == 0

        # the newer Connection is admitted, never refused with a 510
        newer.connect("localhost", port)
        newer.ehlo()
        assert newer.sync(session_id) == 0

        # the take-over is what closes the older Connection, with no reply and
        # nothing acknowledged: it sends nothing itself and still sees the end
        # right away, well before the 60 second idle timeout (spec 7.3, 12.1)
        older.wait_for_close(timeout=5)

        assert newer.send_batch(rfc3164_payloads(3)) == 3

    assert destination.read_logs(3) == expected_messages(3)


def test_altp_starttls_required(config, syslog_ng, port_allocator, testcase_parameters):
    """tls-policy(required) refuses a Session until STARTTLS succeeded (spec 6.1)."""
    port = port_allocator()
    destination = start_altp_receiver(
        config, syslog_ng, port,
        transport="altp(tls-policy(required))",
        tls=tls_options(testcase_parameters),
    )
    session_id = new_session_id()

    with AltpSender() as sender:
        sender.connect("localhost", port)
        assert sender.ehlo() == ["STARTTLS"]
        with pytest.raises(AltpError) as refusal:
            sender.sync(session_id)
        assert refusal.value.code == 505
        assert refusal.value.text == "STARTTLS required"
        sender.wait_for_close()

    with AltpSender() as sender:
        sender.connect("localhost", port)
        assert sender.ehlo() == ["STARTTLS"]
        sender.starttls()
        # capabilities are re-evaluated after the upgrade (spec 5.3, 6.1)
        assert sender.ehlo() == []
        assert sender.sync(session_id) == 0
        assert sender.send_batch(rfc3164_payloads(3)) == 3

    assert destination.read_logs(3) == expected_messages(3)


def test_altp_tls_alone_offers_starttls_and_admits_plaintext(config, syslog_ng, port_allocator, testcase_parameters):
    """A tls() block with no explicit policy offers STARTTLS and still serves plaintext (ADR-0011)."""
    port = port_allocator()
    destination = start_altp_receiver(config, syslog_ng, port, tls=tls_options(testcase_parameters))

    with AltpSender() as sender:
        sender.connect("localhost", port)
        assert sender.ehlo() == ["STARTTLS"]
        assert sender.sync(new_session_id()) == 0
        assert sender.send_batch(rfc3164_payloads(3)) == 3

    assert destination.read_logs(3) == expected_messages(3)


def test_altp_tls_policy_none_withholds_starttls(config, syslog_ng, port_allocator, testcase_parameters):
    """tls-policy(none) advertises no STARTTLS and answers the verb 502, tls() or not (spec 6.1)."""
    port = port_allocator()
    start_altp_receiver(
        config, syslog_ng, port,
        transport="altp(tls-policy(none))",
        tls=tls_options(testcase_parameters),
    )

    with AltpSender() as sender:
        sender.connect("localhost", port)
        assert sender.ehlo() == []
        with pytest.raises(AltpError) as refusal:
            sender.starttls()
        assert refusal.value.code == 502
        sender.wait_for_close()


def test_altp_starttls_optional(config, syslog_ng, port_allocator, testcase_parameters):
    """tls-policy(optional) advertises STARTTLS but accepts a Session in plaintext (spec 6.1)."""
    port = port_allocator()
    source = config.create_network_source(
        ip="localhost",
        port=port,
        transport="altp(tls-policy(optional))",
        tls=tls_options(testcase_parameters),
    )
    destination = config.create_file_destination(file_name="output.txt", template=MESSAGE_TEMPLATE)
    config.create_logpath(statements=[source, destination])

    syslog_ng.start(config)

    with AltpSender() as sender:
        sender.connect("localhost", port)
        assert sender.ehlo() == ["STARTTLS"]
        assert sender.sync(new_session_id()) == 0
        assert sender.send_batch(rfc3164_payloads(3)) == 3

    assert destination.read_logs(3) == expected_messages(3)


def test_altp_zlib_is_not_offered_by_default(config, syslog_ng, port_allocator):
    """Compression is off unless allow-compression() turns it on (spec 6.2)."""
    port = port_allocator()
    start_altp_receiver(config, syslog_ng, port)

    with AltpSender() as sender:
        sender.connect("localhost", port)
        assert sender.ehlo() == []
        with pytest.raises(AltpError) as refusal:
            sender.zlib()
        assert refusal.value.code == 502
        sender.wait_for_close()


def test_altp_zlib_carries_the_whole_session(config, syslog_ng, port_allocator):
    """allow-compression(yes) offers ZLIB, and everything after the reply is deflated (spec 6.2)."""
    port = port_allocator()
    destination = start_altp_receiver(config, syslog_ng, port, transport="altp(allow-compression(yes))")
    session_id = new_session_id()

    with AltpSender() as sender:
        sender.connect("localhost", port)
        assert sender.ehlo() == ["ZLIB"]
        sender.zlib()
        # everything from here on runs inside the zlib stream
        assert sender.ehlo() == []
        assert sender.sync(session_id) == 0
        assert sender.send_batch(rfc3164_payloads(3)) == 3
        sender.noop()

    assert destination.read_logs(3) == expected_messages(3)


def test_altp_zlib_is_answered_507_when_already_compressed(config, syslog_ng, port_allocator):
    """`ZLIB` MUST NOT be requested twice, and `STARTTLS` is not allowed after it (spec 6.1, 6.2)."""
    port = port_allocator()
    start_altp_receiver(config, syslog_ng, port, transport="altp(allow-compression(yes))")

    with AltpSender() as sender:
        sender.connect("localhost", port)
        assert sender.ehlo() == ["ZLIB"]
        sender.zlib()
        with pytest.raises(AltpError) as refusal:
            sender.zlib()
        assert refusal.value.code == 507
        assert refusal.value.text == "Already using ZLIB"
        sender.wait_for_close()


def test_altp_zlib_inside_tls(config, syslog_ng, port_allocator, testcase_parameters):
    """The required ordering is TLS first, then ZLIB inside TLS (spec 6.1, 6.2, Appendix A.2)."""
    port = port_allocator()
    destination = start_altp_receiver(
        config, syslog_ng, port,
        transport="altp(allow-compression(yes))",
        tls=tls_options(testcase_parameters),
    )
    session_id = new_session_id()

    with AltpSender() as sender:
        sender.connect("localhost", port)
        # both Capabilities are offered, so the list is multi-line (spec 5.2)
        assert sender.ehlo() == ["STARTTLS", "ZLIB"]
        sender.starttls()
        assert sender.ehlo() == ["ZLIB"]
        sender.zlib()
        assert sender.sync(session_id) == 0
        assert sender.send_batch(rfc3164_payloads(3)) == 3

    assert destination.read_logs(3) == expected_messages(3)


def test_altp_rejects_oversized_frame(config, syslog_ng, port_allocator):
    """A declared length above log-msg-size() draws 552 and closes (spec 8.2, Appendix A.5)."""
    port = port_allocator()
    destination = start_altp_receiver(config, syslog_ng, port, log_msg_size=8192)

    with AltpSender() as sender:
        sender.connect("localhost", port)
        sender.ehlo()
        assert sender.sync(new_session_id()) == 0
        sender.open_batch()
        # the header is complete once the SP arrives, so no payload is read
        sender.send_frame_header(200000)
        code, lines = sender.read_reply()
        assert (code, lines) == (552, ["Frame too large"])
        sender.wait_for_close()

    path = destination.get_path()
    assert not path.exists() or path.read_text() == ""


def test_altp_syslog_driver(config, syslog_ng, port_allocator):
    """The syslog() driver takes transport(altp) the same way, with RFC5424 payloads."""
    port = port_allocator()
    destination = start_altp_receiver(config, syslog_ng, port, driver="syslog")
    session_id = new_session_id()

    with AltpSender() as sender:
        assert sender.connect("localhost", port) == "ALTP 1.0"
        assert sender.ehlo() == []
        assert sender.sync(session_id) == 0
        assert sender.send_batch(rfc5424_payloads(3)) == 3
        sender.noop()

    assert destination.read_logs(3) == expected_messages(3)
