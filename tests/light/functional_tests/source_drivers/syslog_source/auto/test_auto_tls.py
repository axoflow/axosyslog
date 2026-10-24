#!/usr/bin/env python
#############################################################################
# Copyright (c) 2026 Balazs Scheidler <balazs.scheidler@axoflow.com>
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
import ssl

import pytest
from axosyslog_light.common.file import copy_shared_file
from axosyslog_light.driver_io.network.network_io import NetworkIO

OUTPUT_FILE = "output.log"

# TLS 1.2 record carrying a fatal (2) handshake_failure (40) alert
TLS_ALERT = b"\x15\x03\x03\x00\x02\x02\x28"


def _create_auto_source(config, port_allocator, testcase_parameters=None, tls_optional=None):
    extra_opts = {}
    if testcase_parameters is not None:
        extra_opts["tls"] = {
            "key-file": copy_shared_file(testcase_parameters, "server.key"),
            "cert-file": copy_shared_file(testcase_parameters, "server.crt"),
            "peer-verify": "optional-untrusted",
        }
        if tls_optional is not None:
            extra_opts["tls"]["optional"] = "yes" if tls_optional else "no"

    source = config.create_syslog_source(
        ip="localhost",
        port=port_allocator(),
        transport="auto",
        flags="no-parse",
        **extra_opts,
    )
    file_destination = config.create_file_destination(file_name=OUTPUT_FILE)
    config.create_logpath(statements=[source, file_destination])
    return source, file_destination


def _send_raw(port, payload):
    with socket.create_connection(("localhost", port)) as sock:
        sock.sendall(payload)


def test_auto_accepts_plain_and_tls_on_the_same_source(config, syslog_ng, port_allocator, testcase_parameters):
    source, file_destination = _create_auto_source(config, port_allocator, testcase_parameters, tls_optional=True)
    syslog_ng.start(config)

    for i, (transport, framed) in enumerate([
        (NetworkIO.Transport.TCP, False),
        (NetworkIO.Transport.TCP, True),
        (NetworkIO.Transport.TLS, False),
        (NetworkIO.Transport.TLS, True),
    ]):
        message = f"message {i} {transport.name} framed={framed}"
        source.write_log(message, transport=transport, framed=framed)
        assert file_destination.read_log().endswith(message)


@pytest.mark.parametrize("tls_optional", [None, False], ids=["default", "optional_no"])
def test_auto_rejects_plaintext_unless_tls_is_optional(config, syslog_ng, port_allocator, testcase_parameters, tls_optional):
    source, file_destination = _create_auto_source(config, port_allocator, testcase_parameters, tls_optional=tls_optional)
    syslog_ng.start(config)

    source.write_log("plain message", transport=NetworkIO.Transport.TCP)
    assert syslog_ng.wait_for_message_in_console_log("Non-TLS connection detected, but tls() requires TLS") != []

    source.write_log("tls message", transport=NetworkIO.Transport.TLS)
    assert file_destination.read_log().endswith("tls message")


def test_auto_rejects_tls_without_tls_options(config, syslog_ng, port_allocator):
    source, file_destination = _create_auto_source(config, port_allocator)
    syslog_ng.start(config)

    client_context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    client_context.check_hostname = False
    client_context.verify_mode = ssl.CERT_NONE
    with socket.create_connection(("localhost", source.options["port"])) as sock:
        with pytest.raises((ssl.SSLError, OSError)):
            client_context.wrap_socket(sock).do_handshake()

    assert syslog_ng.wait_for_message_in_console_log("TLS handshake detected, unable to switch to TLS") != []

    # the source keeps accepting plain connections
    source.write_log("plain message", transport=NetworkIO.Transport.TCP)
    assert file_destination.read_log().endswith("plain message")


def test_auto_rejects_tls_alert(config, syslog_ng, port_allocator, testcase_parameters):
    source, _ = _create_auto_source(config, port_allocator, testcase_parameters)
    syslog_ng.start(config)

    _send_raw(source.options["port"], TLS_ALERT)

    assert syslog_ng.wait_for_message_in_console_log("TLS alert detected instead of ClientHello") != []


def test_auto_rejects_binary_data(config, syslog_ng, port_allocator):
    source, file_destination = _create_auto_source(config, port_allocator)
    syslog_ng.start(config)

    _send_raw(source.options["port"], b"\x00\x01\x02\x03 binary garbage\n")

    assert syslog_ng.wait_for_message_in_console_log("Binary data detected during protocol auto-detection") != []

    source.write_log("plain message", transport=NetworkIO.Transport.TCP)
    assert file_destination.read_log().endswith("plain message")
