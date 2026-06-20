#!/usr/bin/env python
#############################################################################
# Copyright (c) 2026 Axoflow
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
import socket
import time

LOOPBACK = "127.0.0.1"


def test_http_source_splits_body_into_messages(config, syslog_ng, port_allocator):
    http_source = config.create_http_source(port=port_allocator(), path="/api/ingest", localip=LOOPBACK)
    file_destination = config.create_file_destination(file_name="output.log", template=r'"${MESSAGE}\n"')
    config.create_logpath(statements=[http_source, file_destination])

    syslog_ng.start(config)

    http_source.write_logs(["first message", "second message", "third message"])

    assert file_destination.read_logs(3) == ["first message", "second message", "third message"]


def test_http_source_captures_client_address(config, syslog_ng, port_allocator):
    http_source = config.create_http_source(port=port_allocator(), path="/in", localip=LOOPBACK)
    file_destination = config.create_file_destination(file_name="output.log", template=r'"${SOURCEIP}\n"')
    config.create_logpath(statements=[http_source, file_destination])

    syslog_ng.start(config)

    http_source.write_log("hello")

    assert file_destination.read_log() == LOOPBACK


def test_http_source_rejects_non_post_method(config, syslog_ng, port_allocator):
    http_source = config.create_http_source(port=port_allocator(), path="/in", localip=LOOPBACK)
    file_destination = config.create_file_destination(file_name="output.log", template=r'"${MESSAGE}\n"')
    config.create_logpath(statements=[http_source, file_destination])

    syslog_ng.start(config)

    assert http_source.get().status_code == 405


def test_http_source_rejects_unknown_path(config, syslog_ng, port_allocator):
    http_source = config.create_http_source(port=port_allocator(), path="/in", localip=LOOPBACK)
    file_destination = config.create_file_destination(file_name="output.log", template=r'"${MESSAGE}\n"')
    config.create_logpath(statements=[http_source, file_destination])

    syslog_ng.start(config)

    assert http_source.post("some data", path="/unknown").status_code == 404


def test_http_source_shares_a_port_between_paths(config, syslog_ng, port_allocator):
    shared_port = port_allocator()

    alpha = config.create_http_source(port=shared_port, path="/alpha", localip=LOOPBACK)
    beta = config.create_http_source(port=shared_port, path="/beta", localip=LOOPBACK)

    alpha_destination = config.create_file_destination(file_name="alpha.log", template=r'"${MESSAGE}\n"')
    beta_destination = config.create_file_destination(file_name="beta.log", template=r'"${MESSAGE}\n"')

    config.create_logpath(statements=[alpha, alpha_destination])
    config.create_logpath(statements=[beta, beta_destination])

    syslog_ng.start(config)

    alpha.write_log("apple")
    beta.write_log("banana")

    assert alpha_destination.read_log() == "apple"
    assert beta_destination.read_log() == "banana"


def test_http_source_applies_backpressure_for_large_request(config, syslog_ng, port_allocator):
    # a small flow-control window combined with a single large multi-line POST
    # forces the connection to be suspended and retried repeatedly; all lines
    # must still be delivered in order without deadlocking
    http_source = config.create_http_source(
        port=port_allocator(), path="/in", localip=LOOPBACK, log_iw_size=128,
    )
    file_destination = config.create_file_destination(file_name="output.log", template=r'"${MESSAGE}\n"')
    config.create_logpath(statements=[http_source, file_destination])

    syslog_ng.start(config)

    messages = [f"message {i}" for i in range(2000)]
    http_source.write_logs(messages)

    assert file_destination.read_logs(2000) == messages


def test_http_source_binds_wildcard_by_default(config, syslog_ng, port_allocator):
    # no localip(): the driver binds a (dual-stack) wildcard, reachable via localhost
    http_source = config.create_http_source(port=port_allocator(), path="/in")
    file_destination = config.create_file_destination(file_name="output.log", template=r'"${MESSAGE}\n"')
    config.create_logpath(statements=[http_source, file_destination])

    syslog_ng.start(config)

    http_source.write_log("wildcard")

    assert file_destination.read_log() == "wildcard"


def test_http_source_times_out_inactive_connection(config, syslog_ng, port_allocator):
    port = port_allocator()
    http_source = config.create_http_source(port=port, path="/in", localip=LOOPBACK, timeout=1)
    file_destination = config.create_file_destination(file_name="output.log", template=r'"${MESSAGE}\n"')
    config.create_logpath(statements=[http_source, file_destination])

    syslog_ng.start(config)

    # send headers but withhold the body, then stall: the server must close the
    # inactive connection after timeout() seconds rather than holding it forever
    sock = socket.create_connection((LOOPBACK, port), timeout=10)
    try:
        sock.sendall(b"POST /in HTTP/1.1\r\nContent-Length: 1000\r\n\r\n")
        sock.settimeout(5)
        start = time.monotonic()
        data = sock.recv(4096)
        elapsed = time.monotonic() - start
        assert data == b""        # the server closed the connection (FIN)
        assert elapsed >= 0.5     # it was the inactivity timer, not an immediate close
    finally:
        sock.close()


def test_http_source_works_across_reload(config, syslog_ng, port_allocator):
    http_source = config.create_http_source(port=port_allocator(), path="/in", localip=LOOPBACK)
    file_destination = config.create_file_destination(file_name="output.log", template=r'"${MESSAGE}\n"')
    config.create_logpath(statements=[http_source, file_destination])

    syslog_ng.start(config)

    http_source.write_log("before reload")
    assert file_destination.read_log() == "before reload"

    syslog_ng.reload(config)

    http_source.write_log("after reload")
    assert file_destination.read_log() == "after reload"
