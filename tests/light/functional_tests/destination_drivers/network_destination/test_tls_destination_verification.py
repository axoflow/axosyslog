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
import datetime
import socket
import ssl
import threading
from pathlib import Path

import pytest
from axosyslog_light.common.blocking import wait_until_true
from cryptography import x509
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.x509.oid import NameOID

SERVER_NAME = "server.example"


def _issue(name, issuer=None, issuer_key=None, ca=True, san=None):
    key = ec.generate_private_key(ec.SECP256R1())
    subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, name)])
    now = datetime.datetime.now(datetime.timezone.utc)
    builder = (
        x509.CertificateBuilder()
        .subject_name(subject)
        .issuer_name(issuer.subject if issuer else subject)
        .public_key(key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(now - datetime.timedelta(minutes=5))
        .not_valid_after(now + datetime.timedelta(days=1))
        .add_extension(x509.BasicConstraints(ca=ca, path_length=None), critical=True)
    )
    if san:
        builder = builder.add_extension(x509.SubjectAlternativeName([x509.DNSName(san)]), critical=False)
    cert = builder.sign(issuer_key or key, hashes.SHA256())
    return cert, key


def _pem(*certs):
    return b"".join(c.public_bytes(serialization.Encoding.PEM) for c in certs)


class _TLSListener:
    def __init__(self, port, chain, key):
        self.received = b""
        self.sni = None
        self._context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self._context.load_cert_chain(chain, key)
        self._context.sni_callback = self._remember_sni
        self._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._socket.bind(("127.0.0.1", port))
        self._socket.listen(5)
        self._socket.settimeout(0.5)
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._serve, daemon=True)
        self._thread.start()

    def _remember_sni(self, ssl_socket, server_name, context):
        self.sni = server_name

    def _serve(self):
        while not self._stop.is_set():
            try:
                connection, _ = self._socket.accept()
            except socket.timeout:
                continue
            connection.settimeout(0.5)
            try:
                with self._context.wrap_socket(connection, server_side=True) as tls:
                    self._read(tls)
            except (ssl.SSLError, OSError):
                pass

    def _read(self, tls):
        while not self._stop.is_set():
            try:
                chunk = tls.recv(4096)
            except socket.timeout:
                continue
            if not chunk:
                return
            self.received += chunk

    def close(self):
        self._stop.set()
        self._thread.join()
        self._socket.close()


def _setup(config, port_allocator, tmp_path, request, tls):
    root, root_key = _issue("root")
    intermediate, intermediate_key = _issue("intermediate", root, root_key)
    leaf, leaf_key = _issue(SERVER_NAME, intermediate, intermediate_key, ca=False, san=SERVER_NAME)

    files = {
        "root.crt": _pem(root),
        "intermediate.crt": _pem(intermediate),
        "chain.crt": _pem(leaf, intermediate),
        "server.key": leaf_key.private_bytes(
            serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption(),
        ),
    }
    for name, data in files.items():
        Path(tmp_path, name).write_bytes(data)

    port = port_allocator()
    listener = _TLSListener(port, Path(tmp_path, "chain.crt"), Path(tmp_path, "server.key"))
    request.addfinalizer(listener.close)

    tls = {
        name: config.stringify(str(Path(tmp_path, value))) if name == "ca-file" else value
        for name, value in tls.items()
    }
    generator_source = config.create_example_msg_generator_source(num=1, template=config.stringify("message text"))
    network_destination = config.create_network_destination(
        ip="127.0.0.1", port=port, transport="tls", time_reopen=1,
        tls={"peer-verify": "yes", **tls},
    )
    config.create_logpath(statements=[generator_source, network_destination])
    return listener


def test_partial_chain_trusts_an_intermediate_ca(config, syslog_ng, port_allocator, tmp_path, request):
    listener = _setup(
        config, port_allocator, tmp_path, request,
        {"ca-file": "intermediate.crt", "ssl-options": "partial-chain", "server-name": config.stringify(SERVER_NAME)},
    )
    syslog_ng.start(config)

    assert wait_until_true(lambda: b"message text" in listener.received)


def test_intermediate_ca_is_rejected_without_partial_chain(config, syslog_ng, port_allocator, tmp_path, request):
    listener = _setup(
        config, port_allocator, tmp_path, request,
        {"ca-file": "intermediate.crt", "server-name": config.stringify(SERVER_NAME)},
    )
    syslog_ng.start(config)

    assert syslog_ng.wait_for_message_in_console_log("Certificate validation failed")
    assert listener.received == b""


@pytest.mark.parametrize(
    "sni, expected_sni", [("yes", SERVER_NAME), (None, None)],
    ids=["sni-yes", "sni-unset"],
)
def test_server_name_is_verified_instead_of_the_address(config, syslog_ng, port_allocator, tmp_path, request, sni, expected_sni):
    listener = _setup(
        config, port_allocator, tmp_path, request,
        {"ca-file": "root.crt", "server-name": config.stringify(SERVER_NAME), "sni": sni},
    )
    syslog_ng.start(config)

    assert wait_until_true(lambda: b"message text" in listener.received)
    assert listener.sni == expected_sni


@pytest.mark.parametrize("server_name", [None, '""'], ids=["unset", "empty"])
def test_address_is_verified_without_server_name(config, syslog_ng, port_allocator, tmp_path, request, server_name):
    listener = _setup(config, port_allocator, tmp_path, request, {"ca-file": "root.crt", "server-name": server_name})
    syslog_ng.start(config)

    assert syslog_ng.wait_for_message_in_console_log("Certificate subject does not match configured hostname; hostname='127.0.0.1'")
    assert listener.received == b""
