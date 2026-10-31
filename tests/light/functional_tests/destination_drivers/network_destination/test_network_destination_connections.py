#!/usr/bin/env python
#############################################################################
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
import selectors
import socket
import time

import pytest

NUM_MESSAGES = 300
TIMEOUT = 10


class MultiConnectionListener:
    """Accepts any number of connections and collects the lines received on each of them."""

    def __init__(self, port):
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.listener.bind(("127.0.0.1", port))
        self.listener.listen(16)
        self.listener.setblocking(False)
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.listener, selectors.EVENT_READ)
        self.buffers = {}
        self.lines = {}

    def _accept(self):
        connection, _ = self.listener.accept()
        connection.setblocking(False)
        self.selector.register(connection, selectors.EVENT_READ)
        self.buffers[connection] = b""
        self.lines[connection] = []

    def _read(self, connection):
        chunk = connection.recv(65536)
        if not chunk:
            self.selector.unregister(connection)
            return
        data = self.buffers[connection] + chunk
        *complete, self.buffers[connection] = data.split(b"\n")
        self.lines[connection].extend(line.decode() for line in complete)

    def read_until(self, num_lines, timeout=TIMEOUT):
        deadline = time.monotonic() + timeout
        while self.num_lines() < num_lines and time.monotonic() < deadline:
            for key, _ in self.selector.select(timeout=0.1):
                if key.fileobj is self.listener:
                    self._accept()
                else:
                    self._read(key.fileobj)
        return self.num_lines() >= num_lines

    def num_lines(self):
        return sum(len(lines) for lines in self.lines.values())

    def lines_per_connection(self):
        return [lines for lines in self.lines.values() if lines]

    def close(self):
        for connection in self.lines:
            connection.close()
        self.listener.close()
        self.selector.close()


@pytest.fixture
def listener(port_allocator):
    listener = MultiConnectionListener(port_allocator())
    yield listener
    listener.close()


def _create_config(config, listener, num_messages, **options):
    generator_source = config.create_example_msg_generator_source(
        num=num_messages, freq=0.0001, template=config.stringify("message $(iterate $(+ $_ 1) 0)"),
    )
    network_destination = config.create_network_destination(
        ip="127.0.0.1", port=listener.listener.getsockname()[1], transport="tcp", **options,
    )
    config.create_logpath(statements=[generator_source, network_destination])


def _message_numbers(lines):
    return sorted(int(line.rsplit(" ", 1)[1]) for line in lines)


def test_messages_are_distributed_between_connections(config, syslog_ng, listener):
    _create_config(config, listener, NUM_MESSAGES, connections=3, batch_size=10)

    syslog_ng.start(config)
    assert listener.read_until(NUM_MESSAGES)

    lines_per_connection = listener.lines_per_connection()
    assert len(lines_per_connection) == 3

    # every message is delivered exactly once
    all_lines = [line for lines in lines_per_connection for line in lines]
    assert _message_numbers(all_lines) == list(range(NUM_MESSAGES))

    # each connection received its messages in their original order
    for lines in lines_per_connection:
        numbers = [int(line.rsplit(" ", 1)[1]) for line in lines]
        assert numbers == sorted(numbers)


def test_messages_with_the_same_partition_key_use_the_same_connection(config, syslog_ng, listener):
    _create_config(
        config, listener, NUM_MESSAGES, connections=3,
        partition_key=config.stringify("$(% $(list-tail $(explode ' ' $MSG)) 2)"),
    )

    syslog_ng.start(config)
    assert listener.read_until(NUM_MESSAGES)

    all_lines = [line for lines in listener.lines_per_connection() for line in lines]
    assert _message_numbers(all_lines) == list(range(NUM_MESSAGES))

    # odd and even messages each stay on a single connection, in order
    for lines in listener.lines_per_connection():
        numbers = [int(line.rsplit(" ", 1)[1]) for line in lines]
        assert numbers == sorted(numbers)
        assert len({n % 2 for n in numbers}) == 1


def test_single_connection_is_the_default(config, syslog_ng, listener):
    _create_config(config, listener, NUM_MESSAGES)

    syslog_ng.start(config)
    assert listener.read_until(NUM_MESSAGES)
    assert len(listener.lines_per_connection()) == 1


def test_datagram_transport_uses_a_socket_per_connection(config, syslog_ng, port_allocator):
    receiver = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    receiver.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
    receiver.bind(("127.0.0.1", port_allocator()))
    receiver.settimeout(0.1)

    generator_source = config.create_example_msg_generator_source(
        num=NUM_MESSAGES, freq=0.001, template=config.stringify("message $(iterate $(+ $_ 1) 0)"),
    )
    network_destination = config.create_network_destination(
        ip="127.0.0.1", port=receiver.getsockname()[1], transport="udp", connections=3, batch_size=10,
    )
    config.create_logpath(statements=[generator_source, network_destination])

    syslog_ng.start(config)

    lines_per_source = {}
    deadline = time.monotonic() + TIMEOUT
    while sum(len(lines) for lines in lines_per_source.values()) < NUM_MESSAGES and time.monotonic() < deadline:
        try:
            datagram, source = receiver.recvfrom(65536)
        except socket.timeout:
            continue
        lines_per_source.setdefault(source, []).append(datagram.decode().rstrip("\n"))
    receiver.close()

    # each connection sends from its own socket, so from its own source port
    assert len(lines_per_source) == 3

    all_lines = [line for lines in lines_per_source.values() for line in lines]
    assert _message_numbers(all_lines) == list(range(NUM_MESSAGES))


def test_connections_are_kept_alive_across_reloads(config, syslog_ng, listener):
    _create_config(config, listener, NUM_MESSAGES, connections=3, batch_size=10)

    syslog_ng.start(config)
    assert listener.read_until(NUM_MESSAGES)
    assert len(listener.lines) == 3

    # the message generator starts over after the reload
    syslog_ng.reload(config)
    assert listener.read_until(2 * NUM_MESSAGES)

    # no new connections were opened
    assert len(listener.lines) == 3
