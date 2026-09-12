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
"""A Lumberjack (Beats protocol) sender for the light framework.

It speaks the sender side of the Lumberjack protocol, versions 1 and 2, on a
raw socket, so that a test sees the exact frames written and the exact
acknowledgements read.  It is not a production sender: nothing is retained and
nothing is retransmitted.  Section numbers refer to the Lumberjack protocol
specification (https://github.com/axoflow/lumberjack-specs).
"""
from __future__ import annotations

import json
import logging
import socket
import ssl
import struct
import typing

from axosyslog_light.common.blocking import DEFAULT_TIMEOUT

logger = logging.getLogger(__name__)

# the port of Beats and Logstash by convention (spec 3.1)
LUMBERJACK_DEFAULT_PORT = 5044

# an ACK frame: version, 'A', uint32 (spec 6.4)
ACK_LENGTH = 6

Payload = typing.Union[bytes, str, typing.Dict[str, typing.Any], typing.List[typing.Tuple[str, str]]]


class LumberjackError(Exception):
    """The receiver did something a sender cannot proceed on."""


class LumberjackSender:
    """One Lumberjack connection, driven synchronously."""

    def __init__(self, version: int = 2, timeout: float = DEFAULT_TIMEOUT) -> None:
        if version not in (1, 2):
            raise ValueError("Lumberjack version must be 1 or 2")
        self.version = version
        self.timeout = timeout
        self.__socket: typing.Optional[socket.socket] = None
        self.__buffer = bytearray()

    def __enter__(self) -> LumberjackSender:
        return self

    def __exit__(self, *exc_info: typing.Any) -> None:
        self.close()

    @property
    def version_byte(self) -> bytes:
        return b"1" if self.version == 1 else b"2"

    def connect(
        self,
        host: str = "localhost",
        port: int = LUMBERJACK_DEFAULT_PORT,
        tls: bool = False,
        cafile: typing.Optional[str] = None,
    ) -> None:
        """Open the connection; with @tls the handshake runs before the first frame (spec 15.2)."""
        sock = socket.create_connection((host, port), timeout=self.timeout)
        if tls:
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
            if cafile:
                context.load_verify_locations(cafile)
            else:
                # the receiver presents the self-signed certificate of shared_files
                context.check_hostname = False
                context.verify_mode = ssl.CERT_NONE
            sock = context.wrap_socket(sock, server_hostname=host)
        self.__socket = sock

    def close(self) -> None:
        if self.__socket is None:
            return
        try:
            self.__socket.close()
        finally:
            self.__socket = None
            self.__buffer.clear()

    # ------------------------------------------------------------------ frames

    @staticmethod
    def encode_window(version: int, size: int) -> bytes:
        return (b"1" if version == 1 else b"2") + b"W" + struct.pack(">I", size)

    @staticmethod
    def encode_json_frame(seq: int, payload: bytes) -> bytes:
        """A version 2 `J` frame (spec 6.2)."""
        return b"2J" + struct.pack(">II", seq, len(payload)) + payload

    @staticmethod
    def encode_data_frame(seq: int, pairs: typing.List[typing.Tuple[str, str]]) -> bytes:
        """A version 1 `D` frame of key/value string pairs (spec 5.2)."""
        frame = bytearray(b"1D" + struct.pack(">II", seq, len(pairs)))
        for key, value in pairs:
            key_bytes = key.encode("utf-8")
            value_bytes = value.encode("utf-8")
            frame += struct.pack(">I", len(key_bytes)) + key_bytes
            frame += struct.pack(">I", len(value_bytes)) + value_bytes
        return bytes(frame)

    def encode_frame(self, seq: int, payload: Payload) -> bytes:
        if self.version == 1:
            if isinstance(payload, dict):
                pairs = [(str(key), str(value)) for key, value in payload.items()]
            elif isinstance(payload, list):
                pairs = payload
            else:
                raise TypeError("a version 1 payload is a dict or a list of (key, value) pairs")
            return self.encode_data_frame(seq, pairs)

        if isinstance(payload, dict):
            payload = json.dumps(payload, separators=(",", ":"))
        if isinstance(payload, str):
            payload = payload.encode("utf-8")
        if not isinstance(payload, bytes):
            raise TypeError("a version 2 payload is a dict, a str or bytes")
        return self.encode_json_frame(seq, payload)

    def encode_window_with_frames(self, payloads: typing.Sequence[Payload]) -> bytes:
        """A `W` frame followed by one data frame per payload, numbered from 1 (spec 7.1, 8.2)."""
        data = bytearray(self.encode_window(self.version, len(payloads)))
        for seq, payload in enumerate(payloads, start=1):
            data += self.encode_frame(seq, payload)
        return bytes(data)

    # ------------------------------------------------------------------ I/O

    def send_raw(self, data: bytes) -> None:
        logger.debug("Lumberjack sender writes %d octets: %r", len(data), data[:64])
        self.__connected_socket().sendall(data)

    def send_window(self, payloads: typing.Sequence[Payload]) -> None:
        """Write a whole window without waiting for its acknowledgement."""
        self.send_raw(self.encode_window_with_frames(payloads))

    def read_ack(self, timeout: typing.Optional[float] = None, skip_keepalives: bool = True) -> int:
        """Read one `A` frame and return its sequence number.

        With @skip_keepalives the A(0) frames a waiting receiver sends are read
        past until the acknowledgement proper arrives (spec 8.4, 10).
        """
        while True:
            frame = self.__read_exact(ACK_LENGTH, timeout)
            version, frame_type = frame[0:1], frame[1:2]
            if frame_type != b"A":
                raise LumberjackError("the receiver sent {!r} instead of an ACK".format(bytes(frame)))
            if version not in (b"1", b"2"):
                raise LumberjackError("the receiver sent an ACK with version byte {!r}".format(bytes(version)))
            (seq,) = struct.unpack(">I", frame[2:6])
            logger.debug("Lumberjack sender read A(%d)", seq)
            if seq == 0 and skip_keepalives:
                continue
            return seq

    def send_batch(self, payloads: typing.Sequence[Payload], timeout: typing.Optional[float] = None) -> int:
        """Write a window and wait for its acknowledgement; returns the acknowledged count."""
        self.send_window(payloads)
        return self.read_ack(timeout)

    def wait_for_close(self, timeout: typing.Optional[float] = None) -> None:
        """Assert that the receiver closed the connection, as it does on a protocol error (spec 14.1)."""
        sock = self.__connected_socket()
        sock.settimeout(self.timeout if timeout is None else timeout)
        try:
            data = sock.recv(4096)
        except (ConnectionResetError, ssl.SSLEOFError):
            return
        if data:
            raise LumberjackError("the receiver sent {!r} instead of closing the connection".format(data))

    def __connected_socket(self) -> socket.socket:
        if self.__socket is None:
            raise LumberjackError("the Lumberjack connection is not open")
        return self.__socket

    def __read_exact(self, length: int, timeout: typing.Optional[float] = None) -> bytes:
        sock = self.__connected_socket()
        sock.settimeout(self.timeout if timeout is None else timeout)
        while len(self.__buffer) < length:
            chunk = sock.recv(4096)
            if not chunk:
                raise LumberjackError("the connection was closed with an incomplete frame: {!r}".format(
                    bytes(self.__buffer),
                ))
            self.__buffer += chunk
        data = bytes(self.__buffer[:length])
        del self.__buffer[:length]
        return data
