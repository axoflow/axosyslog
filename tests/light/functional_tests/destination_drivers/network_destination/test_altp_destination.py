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
"""End-to-end tests of the ALTP Sender: `transport(altp(...))` of the network()
destination driver.

The loopback tests send through an ALTP destination into an ALTP source of the
same instance; the ones using AltpFakeReceiver assert the exact octets the
Sender writes and drive the resume of spec 10.3.
"""
import re
import socket
import typing
import zlib

from axosyslog_light.common.file import copy_shared_file

MESSAGE_TEMPLATE = r'"${MESSAGE}\n"'

# a Session ID a Sender SHOULD generate: 16 octets in lowercase hex (spec 7.1)
SESSION_ID_PATTERN = re.compile(r"^[0-9a-f]{32}$")


def messages(count: int, first: int = 0) -> typing.List[str]:
    return ["message-{}".format(index) for index in range(first, first + count)]


def frames_of(payloads: typing.List[str]) -> typing.List[bytes]:
    """What the Frames of @payloads look like with template("${MESSAGE}\\n")."""
    return [(payload + "\n").encode("utf-8") for payload in payloads]


class AltpFakeReceiver:
    """The Receiver side of ALTP on a raw socket, one connection at a time."""

    def __init__(self, port: int, timeout: float = 15.0) -> None:
        self.port = port
        self.timeout = timeout
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.listener.bind(("localhost", port))
        self.listener.listen(1)
        self.connection = None
        self.buffer = b""
        # each direction is one continuous zlib stream once ZLIB is negotiated (spec 6.2)
        self.deflate = None
        self.inflate = None

    def close(self) -> None:
        if self.connection is not None:
            self.connection.close()
            self.connection = None
        self.buffer = b""
        self.deflate = None
        self.inflate = None

    def stop(self) -> None:
        self.close()
        self.listener.close()

    def accept(self) -> None:
        self.close()
        self.listener.settimeout(self.timeout)
        self.connection, _ = self.listener.accept()
        self.connection.settimeout(self.timeout)

    def send(self, reply: bytes) -> None:
        if self.deflate is not None:
            reply = self.deflate.compress(reply) + self.deflate.flush(zlib.Z_SYNC_FLUSH)
        self.connection.sendall(reply)

    def __recv(self) -> None:
        chunk = self.connection.recv(4096)
        assert chunk, "the ALTP Sender closed the connection"
        if self.inflate is not None:
            # a chunk may hold no complete deflate block yet, and then yields nothing
            chunk = self.inflate.decompress(chunk)
        self.buffer += chunk

    def __read_exactly(self, length: int) -> bytes:
        while len(self.buffer) < length:
            self.__recv()
        data, self.buffer = self.buffer[:length], self.buffer[length:]
        return data

    def read_line(self) -> bytes:
        while b"\n" not in self.buffer:
            self.__recv()
        line, _, self.buffer = self.buffer.partition(b"\n")
        return line + b"\n"

    def read_frame_or_terminator(self) -> typing.Optional[bytes]:
        """One Frame of the open Batch, or None on the terminator line (spec 8.2, 8.3)."""
        header = b""
        while not header.endswith(b" "):
            octet = self.__read_exactly(1)
            if octet == b".":
                assert self.__read_exactly(1) == b"\n", "the Batch terminator is a line of a single dot (8.3)"
                assert header == b"", "a Frame header must be digits followed by a single SP (8.2)"
                return None
            header += octet

        digits = header[:-1].decode("ascii")
        assert digits.isdigit() and not digits.startswith("0"), "a Frame length is decimal with no leading zero (8.2)"

        return self.__read_exactly(int(digits))

    def read_batch(self) -> typing.List[bytes]:
        frames = []
        while True:
            frame = self.read_frame_or_terminator()
            if frame is None:
                return frames
            frames.append(frame)

    def expect_banner_and_ehlo(self) -> None:
        """A Sender writes nothing before the banner and answers it with EHLO (spec 5.1, 5.2)."""
        self.send(b"220 ALTP 1.0\n")
        assert self.read_line() == b"EHLO 1.0\n"

    def read_sync(self) -> str:
        """The Session ID of the SYNC the Sender opens its Session with (spec 7.2)."""
        line = self.read_line()
        assert line.startswith(b"SYNC "), "the Sender did not open a Session: {!r}".format(line)
        session_id = line[len(b"SYNC "):-1].decode("ascii")
        assert SESSION_ID_PATTERN.match(session_id), "not a Session ID of 128 random bits: {!r}".format(session_id)
        return session_id

    def expect_sync(self) -> str:
        """The Session ID of the SYNC that follows an empty Capability list (spec 7.2)."""
        self.send(b"250 \n")
        return self.read_sync()

    def negotiate_zlib(self) -> None:
        """Advertise ZLIB, accept the command and compress both directions (spec 6.2).

        Compression begins with the first octet written after the LF of the
        reply, so the two streams are created only once it has been sent.
        """
        self.send(b"250 ZLIB\n")
        line = self.read_line()
        assert line == b"ZLIB\n", "the Sender did not request the advertised compression: {!r}".format(line)
        assert not self.buffer, \
            "the Sender pipelined uncompressed octets after ZLIB: {!r}".format(self.buffer)

        self.send(b"250 Ready to start ZLIB\n")
        self.deflate = zlib.compressobj()
        self.inflate = zlib.decompressobj()

    def acknowledge_and_expect_data(self, frames_acked: int) -> None:
        """`250 Received n`, which the Sender answers with a fresh DATA (ADR-0010)."""
        self.send("250 Received {}\n".format(frames_acked).encode("ascii"))
        assert self.read_line() == b"DATA\n"
        self.send(b"250 Ready\n")

    def open_session(self, frames_acked: int = 0, compressed: bool = False) -> str:
        """Banner to `250 Ready`: everything before the first Frame may be written."""
        self.accept()
        self.expect_banner_and_ehlo()
        if compressed:
            self.negotiate_zlib()
            session_id = self.read_sync()
        else:
            session_id = self.expect_sync()
        self.acknowledge_and_expect_data(frames_acked)
        return session_id


def _build_config(config, port_allocator, receiver_port, transport="altp", **destination_options):
    """A plain TCP source feeding an ALTP destination that talks to @receiver_port."""
    source = config.create_network_source(ip="localhost", port=port_allocator(), flags="no-parse")
    destination = config.create_network_destination(
        ip="localhost", port=receiver_port, transport=transport,
        template=MESSAGE_TEMPLATE, time_reopen=1,
        **destination_options,
    )
    config.create_logpath(statements=[source, destination])

    return source


def test_altp_destination_delivers_through_an_altp_source(config, syslog_ng, port_allocator):
    """The loopback of a deployment: an ALTP destination into an ALTP source."""
    altp_port = port_allocator()

    source = config.create_network_source(ip="localhost", port=port_allocator(), flags="no-parse")
    altp_destination = config.create_network_destination(
        ip="localhost", port=altp_port, transport="altp",
        template=MESSAGE_TEMPLATE, time_reopen=1,
    )
    config.create_logpath(statements=[source, altp_destination])

    altp_source = config.create_network_source(ip="localhost", port=altp_port, transport="altp", flags="no-parse")
    file_destination = config.create_file_destination(file_name="output.txt", template=MESSAGE_TEMPLATE)
    config.create_logpath(statements=[altp_source, file_destination])

    syslog_ng.start(config)

    source.write_logs(messages(5))

    assert file_destination.read_logs(5) == messages(5)


def test_altp_destination_delivers_over_tls(config, syslog_ng, port_allocator, testcase_parameters):
    """A tls() block on either side offers STARTTLS, and both ends take it up (spec 6.1, ADR-0011)."""
    altp_port = port_allocator()

    source = config.create_network_source(ip="localhost", port=port_allocator(), flags="no-parse")
    altp_destination = config.create_network_destination(
        ip="localhost", port=altp_port, transport="altp",
        template=MESSAGE_TEMPLATE, time_reopen=1,
        tls={
            "ca-file": copy_shared_file(testcase_parameters, "valid-ca.crt"),
            "peer-verify": "yes",
        },
    )
    config.create_logpath(statements=[source, altp_destination])

    altp_source = config.create_network_source(
        ip="localhost", port=altp_port, transport="altp", flags="no-parse",
        tls={
            "key-file": copy_shared_file(testcase_parameters, "valid-localhost.key"),
            "cert-file": copy_shared_file(testcase_parameters, "valid-localhost.crt"),
            "peer-verify": "optional-untrusted",
        },
    )
    file_destination = config.create_file_destination(file_name="output.txt", template=MESSAGE_TEMPLATE)
    config.create_logpath(statements=[altp_source, file_destination])

    syslog_ng.start(config)

    source.write_logs(messages(5))

    assert file_destination.read_logs(5) == messages(5)
    # tls() alone is the optional policy, so delivery alone would not prove the upgrade
    assert syslog_ng.wait_for_message_in_console_log("ALTP Connection switched to TLS") != []


def test_altp_legacy_option_spellings_deliver_end_to_end(config, syslog_ng, port_allocator, testcase_parameters):
    """A configuration written with the option spellings of an existing ALTP deployment keeps working."""
    altp_port = port_allocator()

    source = config.create_network_source(ip="localhost", port=port_allocator(), flags="no-parse")
    altp_destination = config.create_network_destination(
        ip="localhost", port=altp_port,
        transport="altp(tls_required(yes) allow_plain_compress(yes) compress_level(6) batch_size(100) "
                  "message_acknowledgement_timeout(30))",
        template=MESSAGE_TEMPLATE, time_reopen=1,
        tls={
            "ca-file": copy_shared_file(testcase_parameters, "valid-ca.crt"),
            "peer-verify": "yes",
        },
    )
    config.create_logpath(statements=[source, altp_destination])

    altp_source = config.create_network_source(
        ip="localhost", port=altp_port, flags="no-parse",
        transport="altp(tls_required(yes) allow_plain_compress(yes) compress_level(6))",
        tls={
            "key-file": copy_shared_file(testcase_parameters, "valid-localhost.key"),
            "cert-file": copy_shared_file(testcase_parameters, "valid-localhost.crt"),
            "peer-verify": "optional-untrusted",
        },
    )
    file_destination = config.create_file_destination(file_name="output.txt", template=MESSAGE_TEMPLATE)
    config.create_logpath(statements=[altp_source, file_destination])

    syslog_ng.start(config)

    source.write_logs(messages(5))

    assert file_destination.read_logs(5) == messages(5)
    # tls_required(yes) refuses a plaintext Session, so delivery proves the upgrade
    assert syslog_ng.wait_for_message_in_console_log("ALTP Connection switched to TLS") != []


def test_altp_destination_and_source_compress_end_to_end(config, syslog_ng, port_allocator):
    """The loopback of a compressed deployment: compression() into allow-compression()."""
    altp_port = port_allocator()

    source = config.create_network_source(ip="localhost", port=port_allocator(), flags="no-parse")
    altp_destination = config.create_network_destination(
        ip="localhost", port=altp_port, transport="altp(compression(yes))",
        template=MESSAGE_TEMPLATE, time_reopen=1,
    )
    config.create_logpath(statements=[source, altp_destination])

    altp_source = config.create_network_source(
        ip="localhost", port=altp_port, transport="altp(allow-compression(yes))", flags="no-parse",
    )
    file_destination = config.create_file_destination(file_name="output.txt", template=MESSAGE_TEMPLATE)
    config.create_logpath(statements=[altp_source, file_destination])

    syslog_ng.start(config)

    source.write_logs(messages(5))

    assert file_destination.read_logs(5) == messages(5)


def test_altp_destination_compresses_inside_tls(config, syslog_ng, port_allocator, testcase_parameters):
    """The required ordering is TLS first, then ZLIB inside TLS (spec 6.1, 6.2, Appendix A.2)."""
    altp_port = port_allocator()

    source = config.create_network_source(ip="localhost", port=port_allocator(), flags="no-parse")
    altp_destination = config.create_network_destination(
        ip="localhost", port=altp_port, transport="altp(compression(yes))",
        template=MESSAGE_TEMPLATE, time_reopen=1,
        tls={
            "ca-file": copy_shared_file(testcase_parameters, "valid-ca.crt"),
            "peer-verify": "yes",
        },
    )
    config.create_logpath(statements=[source, altp_destination])

    altp_source = config.create_network_source(
        ip="localhost", port=altp_port, transport="altp(allow-compression(yes))", flags="no-parse",
        tls={
            "key-file": copy_shared_file(testcase_parameters, "valid-localhost.key"),
            "cert-file": copy_shared_file(testcase_parameters, "valid-localhost.crt"),
            "peer-verify": "optional-untrusted",
        },
    )
    file_destination = config.create_file_destination(file_name="output.txt", template=MESSAGE_TEMPLATE)
    config.create_logpath(statements=[altp_source, file_destination])

    syslog_ng.start(config)

    source.write_logs(messages(5))

    assert file_destination.read_logs(5) == messages(5)


def test_altp_destination_requests_zlib_and_deflates_its_frames(config, syslog_ng, port_allocator):
    """The `ZLIB` sequence of Appendix A.2, seen from the Receiver."""
    receiver = AltpFakeReceiver(port_allocator())
    try:
        source = _build_config(config, port_allocator, receiver.port,
                               transport="altp(compression(yes))", flush_lines=10)
        syslog_ng.start(config)

        receiver.open_session(compressed=True)

        source.write_logs(messages(2))
        assert receiver.read_batch() == frames_of(messages(2))

        receiver.acknowledge_and_expect_data(2)

        source.write_logs(messages(1, first=2))
        assert receiver.read_batch() == frames_of(messages(1, first=2))
    finally:
        receiver.stop()


def test_altp_destination_stays_in_the_clear_when_zlib_is_not_advertised(config, syslog_ng, port_allocator):
    """A Sender MUST NOT invoke a Capability the last EHLO reply did not advertise,
    so compression() alone does not put ZLIB on the wire (spec 5.3)."""
    receiver = AltpFakeReceiver(port_allocator())
    try:
        source = _build_config(config, port_allocator, receiver.port,
                               transport="altp(compression(yes))", flush_lines=10)
        syslog_ng.start(config)

        # the Capability list is empty, so the Frames arrive uncompressed
        receiver.open_session()

        source.write_logs(messages(2))
        assert receiver.read_batch() == frames_of(messages(2))
    finally:
        receiver.stop()


def test_altp_destination_writes_the_commands_and_frames_of_the_specification(config, syslog_ng, port_allocator):
    """The minimal plaintext session of Appendix A.1, seen from the Receiver."""
    receiver = AltpFakeReceiver(port_allocator())
    try:
        source = _build_config(config, port_allocator, receiver.port, flush_lines=10)
        syslog_ng.start(config)

        receiver.open_session()

        source.write_logs(messages(2))
        assert receiver.read_batch() == frames_of(messages(2))

        receiver.acknowledge_and_expect_data(2)

        source.write_logs(messages(1, first=2))
        assert receiver.read_batch() == frames_of(messages(1, first=2))
    finally:
        receiver.stop()


def test_altp_destination_releases_the_frames_the_next_sync_reports(config, syslog_ng, port_allocator):
    """A Batch interrupted by a lost connection is reconciled by the next SYNC (spec 10.3)."""
    receiver = AltpFakeReceiver(port_allocator())
    try:
        source = _build_config(config, port_allocator, receiver.port, flush_lines=10)
        syslog_ng.start(config)

        session_id = receiver.open_session()

        source.write_logs(messages(2))
        assert receiver.read_batch() == frames_of(messages(2))

        # lost before the acknowledgement: the Sender retains both Frames
        receiver.close()

        receiver.accept()
        receiver.expect_banner_and_ehlo()
        assert receiver.expect_sync() == session_id, "the Session ID MUST NOT change across reconnections (7.1)"

        # both Frames became durable after all, so the Sender resends nothing
        receiver.acknowledge_and_expect_data(2)

        source.write_logs(messages(1, first=2))
        assert receiver.read_batch() == frames_of(messages(1, first=2))
    finally:
        receiver.stop()


def test_altp_destination_resends_the_frames_beyond_a_partial_acknowledgement(config, syslog_ng, port_allocator):
    """The Frames beyond the count are undelivered and are resent in a new Batch (spec 9.3, 9.4)."""
    receiver = AltpFakeReceiver(port_allocator())
    try:
        source = _build_config(config, port_allocator, receiver.port, flush_lines=10)
        syslog_ng.start(config)

        receiver.open_session()

        source.write_logs(messages(3))
        assert receiver.read_batch() == frames_of(messages(3))

        # only the first Frame is durable: the other two are resent in their
        # original order, in the Batch the fresh DATA opened
        receiver.acknowledge_and_expect_data(1)
        assert receiver.read_batch() == frames_of(messages(2, first=1))

        receiver.acknowledge_and_expect_data(2)
        source.write_logs(messages(1, first=3))
        assert receiver.read_batch() == frames_of(messages(1, first=3))
    finally:
        receiver.stop()


def test_altp_destination_bounds_its_batch_by_the_frame_count(config, syslog_ng, port_allocator):
    """flush-lines() bounds the Batch, and the Batch is the unit of acknowledgement (spec 8.4)."""
    receiver = AltpFakeReceiver(port_allocator())
    try:
        source = _build_config(config, port_allocator, receiver.port, flush_lines=2)
        syslog_ng.start(config)

        receiver.open_session()

        source.write_logs(messages(4))

        assert receiver.read_batch() == frames_of(messages(2))
        receiver.acknowledge_and_expect_data(2)
        assert receiver.read_batch() == frames_of(messages(2, first=2))
        receiver.acknowledge_and_expect_data(2)
    finally:
        receiver.stop()


def test_altp_destination_drops_a_message_larger_than_max_frame_size(config, syslog_ng, port_allocator):
    """An oversized message is dropped rather than sent, because a Frame drawing
    `552 Frame too large` would abort every later Connection (spec 8.5)."""
    receiver = AltpFakeReceiver(port_allocator())
    try:
        source = _build_config(config, port_allocator, receiver.port, transport="altp(max-frame-size(64))")
        syslog_ng.start(config)

        receiver.open_session()

        source.write_logs(["x" * 100] + messages(1))

        # the oversized message never reaches the wire, the one after it does
        assert receiver.read_batch() == frames_of(messages(1))
    finally:
        receiver.stop()
