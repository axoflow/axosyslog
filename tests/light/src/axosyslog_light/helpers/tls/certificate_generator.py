#!/usr/bin/env python
#############################################################################
# Copyright (c) 2026 Axoflow
# Copyright (c) 2026 Attila Szakacs <attila.szakacs@axoflow.com>
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
import ipaddress
from dataclasses import dataclass
from pathlib import Path

from cryptography import x509
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import ExtendedKeyUsageOID
from cryptography.x509.oid import NameOID


@dataclass
class TLSCertificates:
    ca_cert: Path
    server_cert: Path
    server_key: Path
    client_cert: Path
    client_key: Path


def _new_key():
    return rsa.generate_private_key(public_exponent=65537, key_size=2048)


def _write_key(key, path: Path) -> None:
    path.write_bytes(
        key.private_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PrivateFormat.TraditionalOpenSSL,
            encryption_algorithm=serialization.NoEncryption(),
        ),
    )


def _write_cert(cert, path: Path) -> None:
    path.write_bytes(cert.public_bytes(serialization.Encoding.PEM))


def _sign(builder, issuer_key):
    return builder.sign(private_key=issuer_key, algorithm=hashes.SHA256())


def generate_tls_certificates(directory, hostnames=None, ip_addresses=None) -> TLSCertificates:
    """Generate a CA and a CA-signed server and client certificate.

    The server certificate carries the given hostnames and IP addresses as
    subjectAltName entries, which peers (e.g. gRPC) verify against the connect
    address. Files are written into directory as PEM and their paths returned.
    """
    directory = Path(directory)
    hostnames = hostnames if hostnames is not None else ["localhost"]
    ip_addresses = ip_addresses if ip_addresses is not None else ["127.0.0.1"]

    not_before = datetime.datetime.now(datetime.timezone.utc) - datetime.timedelta(days=1)
    not_after = not_before + datetime.timedelta(days=3650)

    ca_key = _new_key()
    ca_name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "AxoSyslog Light Test CA")])
    ca_cert = _sign(
        x509.CertificateBuilder()
        .subject_name(ca_name)
        .issuer_name(ca_name)
        .public_key(ca_key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(not_before)
        .not_valid_after(not_after)
        .add_extension(x509.BasicConstraints(ca=True, path_length=None), critical=True)
        .add_extension(
            x509.KeyUsage(
                digital_signature=True, key_cert_sign=True, crl_sign=True,
                key_encipherment=False, content_commitment=False, data_encipherment=False,
                key_agreement=False, encipher_only=False, decipher_only=False,
            ), critical=True,
        ),
        ca_key,
    )

    san = [x509.DNSName(h) for h in hostnames]
    san += [x509.IPAddress(ipaddress.ip_address(ip)) for ip in ip_addresses]

    server_key = _new_key()
    server_cert = _sign(
        x509.CertificateBuilder()
        .subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, hostnames[0])]))
        .issuer_name(ca_name)
        .public_key(server_key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(not_before)
        .not_valid_after(not_after)
        .add_extension(x509.SubjectAlternativeName(san), critical=False)
        .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), critical=False),
        ca_key,
    )

    client_key = _new_key()
    client_cert = _sign(
        x509.CertificateBuilder()
        .subject_name(x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "axosyslog-light-client")]))
        .issuer_name(ca_name)
        .public_key(client_key.public_key())
        .serial_number(x509.random_serial_number())
        .not_valid_before(not_before)
        .not_valid_after(not_after)
        .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.CLIENT_AUTH]), critical=False),
        ca_key,
    )

    certs = TLSCertificates(
        ca_cert=directory / "ca.crt",
        server_cert=directory / "server.crt",
        server_key=directory / "server.key",
        client_cert=directory / "client.crt",
        client_key=directory / "client.key",
    )

    _write_cert(ca_cert, certs.ca_cert)
    _write_cert(server_cert, certs.server_cert)
    _write_key(server_key, certs.server_key)
    _write_cert(client_cert, certs.client_cert)
    _write_key(client_key, certs.client_key)

    return certs
