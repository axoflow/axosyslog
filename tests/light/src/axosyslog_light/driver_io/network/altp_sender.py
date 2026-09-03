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
"""An ALTP Sender for the light framework.

It speaks the Sender side of ALTP 1.0 on a raw socket, so that a test can drive
a Receiver command by command.  It is not a production Sender: frames are never
retained and no state is persisted.
"""
from __future__ import annotations

import logging
import socket
import ssl
import typing
import zlib

from axosyslog_light.common.blocking import DEFAULT_TIMEOUT

logger = logging.getLogger(__name__)

# the default TCP port of an ALTP Receiver (spec 4.1)
ALTP_DEFAULT_PORT = 35514

# a command or reply line, its terminator included (spec 4.2, 4.3)
ALTP_MAX_LINE_LENGTH = 512


class AltpError(Exception):
    """A reply the Sender cannot proceed on: a non-2xx code, or a malformed reply."""

    def __init__(
        self,
        message: str,
        code: typing.Optional[int] = None,
        lines: typing.Optional[typing.List[str]] = None,
    ) -> None:
        self.code = code
        self.lines = list(lines) if lines else []
        if code is not None:
            message = "{} (code={}, text={!r})".format(message, code, self.text)
        super().__init__(message)

    @property
    def text(self) -> str:
        return self.lines[0] if self.lines else ""


class AltpSender:
    """One ALTP Connection, driven synchronously."""

    def __init__(self, timeout: float = DEFAULT_TIMEOUT) -> None:
        self.timeout = timeout
        self.banner = ""
        self.__host = "localhost"
        self.__socket: typing.Optional[socket.socket] = None
        self.__buffer = bytearray()
        # each direction is one continuous zlib stream once ZLIB is negotiated (spec 6.2)
        self.__deflate: typing.Optional["zlib._Compress"] = None
        self.__inflate: typing.Optional["zlib._Decompress"] = None

    def __enter__(self) -> AltpSender:
        return self

    def __exit__(self, *exc_info: typing.Any) -> None:
        self.close()

    def connect(self, host: str = "localhost", port: int = ALTP_DEFAULT_PORT) -> str:
        """Open the Connection and read the banner (spec 5.1)."""
        self.__host = host
        self.__socket = socket.create_connection((host, port), timeout=self.timeout)
        code, lines = self.read_reply()
        if code // 100 != 2:
            raise AltpError("the ALTP banner is not a 2xx reply", code, lines)
        self.banner = lines[0]
        return self.banner

    def close(self) -> None:
        if self.__socket is None:
            return
        try:
            self.__socket.close()
        finally:
            self.__socket = None
            self.__buffer.clear()
            self.__deflate = None
            self.__inflate = None

    def read_reply(self, timeout: typing.Optional[float] = None) -> typing.Tuple[int, typing.List[str]]:
        """Read one reply, single or multi-line, and return its code and text lines.

        Line text is verbatim, so the empty capability list `250 \\n` comes
        back as ``(250, [""])`` (spec 4.3).
        """
        lines: typing.List[str] = []
        code = None
        while True:
            line = self.__read_line(timeout)
            if len(line) < 4 or not line[:3].isdigit() or line[3] not in " -":
                raise AltpError("malformed ALTP reply line: {!r}".format(line))
            line_code = int(line[:3])
            if code is not None and line_code != code:
                raise AltpError("every line of a multi-line ALTP reply must carry the same code: {!r}".format(line))
            code = line_code
            lines.append(line[4:])
            if line[3] == " ":
                logger.debug("ALTP reply read: code=%d, lines=%s", code, lines)
                return code, lines

    def ehlo(self, version: typing.Optional[str] = "1.0") -> typing.List[str]:
        """Negotiate the dialect and return the advertised capabilities (spec 5.2)."""
        command = "EHLO" if version is None else "EHLO {}".format(version)
        _, lines = self.__command(command)
        return [capability for capability in lines if capability]

    def starttls(self, cafile: typing.Optional[str] = None) -> None:
        """Upgrade the Connection to TLS (spec 6.1).

        The handshake begins with the first octet written after the LF of the
        reply, so the socket is wrapped only once the reply has been read.
        """
        self.__command("STARTTLS")
        assert not self.__buffer, \
            "the Receiver sent plaintext after the STARTTLS reply: {!r}".format(bytes(self.__buffer))

        context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
        if cafile:
            context.load_verify_locations(cafile)
        else:
            # the Receiver presents the self-signed certificate of shared_files
            context.check_hostname = False
            context.verify_mode = ssl.CERT_NONE
        self.__socket = context.wrap_socket(
            self.__connected_socket(),
            server_hostname=self.__host if cafile else None,
        )

    def zlib(self) -> None:
        """Compress the Connection in both directions (spec 6.2).

        Compression begins with the first octet either party writes after the
        LF of the reply, so the two streams are created only once it is read.
        """
        self.__command("ZLIB")
        assert not self.__buffer, \
            "the Receiver sent uncompressed octets after the ZLIB reply: {!r}".format(bytes(self.__buffer))

        self.__deflate = zlib.compressobj()
        self.__inflate = zlib.decompressobj()

    def noop(self) -> None:
        """Keep the Connection alive (spec 6.3)."""
        self.__command("NOOP")

    def send_command(self, command: str) -> None:
        """Write one command line without reading its reply."""
        self.__write(command.encode("utf-8") + b"\n")

    def sync(self, session_id: str, timeout: typing.Optional[float] = None) -> int:
        """Bind the Connection to a Session and return the durable frame count (spec 7.2)."""
        code, lines = self.__command("SYNC {}".format(session_id), timeout=timeout)
        return self.__parse_received(code, lines)

    def open_batch(self) -> None:
        """Open a Batch and wait for `250 Ready` (spec 8.1)."""
        self.__command("DATA")

    def send_frame(self, payload: bytes) -> None:
        """Write one octet-counted frame: the header, then the payload, no delimiter (spec 8.2)."""
        self.__write("{} ".format(len(payload)).encode("ascii") + payload)

    def send_frame_header(self, length: int) -> None:
        """Write a frame header alone, to exercise the Receiver's frame size limit."""
        self.__write("{} ".format(length).encode("ascii"))

    def close_batch(self, timeout: typing.Optional[float] = None) -> int:
        """Terminate the Batch and return the acknowledged count (spec 8.3, 9.2).

        The reply is deferred until the frames are durable at the Receiver, so
        a Batch whose destination is slow needs a longer @timeout.
        """
        self.__write(b".\n")
        code, lines = self.read_reply(timeout)
        return self.__parse_received(code, lines)

    def send_batch(self, payloads: typing.List[bytes], timeout: typing.Optional[float] = None) -> int:
        """Send one whole Batch and return the acknowledged count."""
        self.open_batch()
        for payload in payloads:
            self.send_frame(payload)
        return self.close_batch(timeout)

    def wait_for_close(self, timeout: typing.Optional[float] = None) -> None:
        """Assert that the Receiver closed the Connection, as it does after every 5xx reply."""
        sock = self.__connected_socket()
        sock.settimeout(self.timeout if timeout is None else timeout)
        try:
            data = sock.recv(4096)
        except (ConnectionResetError, ssl.SSLEOFError):
            return
        if data:
            raise AltpError("the Receiver sent {!r} instead of closing the Connection".format(data))

    def __connected_socket(self) -> socket.socket:
        if self.__socket is None:
            raise AltpError("the ALTP Connection is not open")
        return self.__socket

    def __command(self, command: str, timeout: typing.Optional[float] = None) -> typing.Tuple[int, typing.List[str]]:
        """Write one command line and read the 2xx reply it must draw."""
        self.__write(command.encode("utf-8") + b"\n")
        code, lines = self.read_reply(timeout)
        if code // 100 != 2:
            raise AltpError("the ALTP {} command was refused".format(command.split(" ")[0]), code, lines)
        return code, lines

    def __parse_received(self, code: int, lines: typing.List[str]) -> int:
        """`250 Received <n>` is the one reply text a Sender parses (spec 13.2)."""
        prefix = "Received "
        text = lines[-1]
        if len(lines) != 1 or not text.startswith(prefix) or not text[len(prefix):].isdigit():
            raise AltpError("malformed ALTP acknowledgement", code, lines)
        return int(text[len(prefix):])

    def __write(self, data: bytes) -> None:
        logger.debug("ALTP Sender writes: %r", data)
        if self.__deflate is not None:
            data = self.__deflate.compress(data) + self.__deflate.flush(zlib.Z_SYNC_FLUSH)
        self.__connected_socket().sendall(data)

    def __read_line(self, timeout: typing.Optional[float] = None) -> str:
        sock = self.__connected_socket()
        sock.settimeout(self.timeout if timeout is None else timeout)
        while True:
            lf = self.__buffer.find(b"\n")
            if lf >= 0:
                line = self.__buffer[:lf]
                del self.__buffer[:lf + 1]
                return line.decode("utf-8", errors="replace")
            if len(self.__buffer) >= ALTP_MAX_LINE_LENGTH:
                raise AltpError("ALTP reply line exceeds {} octets: {!r}".format(
                    ALTP_MAX_LINE_LENGTH, bytes(self.__buffer),
                ))
            chunk = sock.recv(4096)
            if not chunk:
                raise AltpError("the ALTP Connection was closed with an incomplete reply: {!r}".format(
                    bytes(self.__buffer),
                ))
            if self.__inflate is not None:
                # a chunk may hold no complete deflate block yet, and then yields nothing
                chunk = self.__inflate.decompress(chunk)
            self.__buffer += chunk
