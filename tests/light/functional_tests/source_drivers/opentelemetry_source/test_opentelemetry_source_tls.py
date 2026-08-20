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
from pathlib import Path

from axosyslog_light.helpers.tls.certificate_generator import generate_tls_certificates
from axosyslog_light.syslog_ng_config.__init__ import stringify

CLIENT_ORGANIZATION = "AxoSyslog Test Org"
CLIENT_ORGANIZATIONAL_UNIT = "AxoSyslog Test Unit"

TEMPLATE = stringify("${.tls.x509_cn}|${.tls.x509_o}|${.tls.x509_ou}\n")


def _configure(config, port, certs, peer_verify, mutual):
    otel_source = config.create_opentelemetry_source(
        port=port,
        auth={
            "tls": {
                "key-file": stringify(str(certs.server_key)),
                "cert-file": stringify(str(certs.server_cert)),
                "ca-file": stringify(str(certs.ca_cert)),
                "peer-verify": peer_verify,
            },
        },
    )
    otel_source.set_tls(
        certs.ca_cert,
        certs.client_cert if mutual else None,
        certs.client_key if mutual else None,
    )

    file_destination = config.create_file_destination(file_name="output.log", template=TEMPLATE)
    config.create_logpath(statements=[otel_source, file_destination])

    return otel_source, file_destination


def test_opentelemetry_source_tls_peer_info(config, syslog_ng, port_allocator):
    certs = generate_tls_certificates(
        Path.cwd(),
        client_organization=CLIENT_ORGANIZATION,
        client_organizational_unit=CLIENT_ORGANIZATIONAL_UNIT,
    )
    otel_source, file_destination = _configure(config, port_allocator(), certs, "required-trusted", mutual=True)

    syslog_ng.start(config)
    otel_source.write_log()

    assert file_destination.read_log() == f"axosyslog-light-client|{CLIENT_ORGANIZATION}|{CLIENT_ORGANIZATIONAL_UNIT}"


def test_opentelemetry_source_tls_peer_info_without_client_certificate(config, syslog_ng, port_allocator):
    certs = generate_tls_certificates(Path.cwd())
    otel_source, file_destination = _configure(config, port_allocator(), certs, "optional-untrusted", mutual=False)

    syslog_ng.start(config)
    otel_source.write_log()

    assert file_destination.read_log() == "||"
