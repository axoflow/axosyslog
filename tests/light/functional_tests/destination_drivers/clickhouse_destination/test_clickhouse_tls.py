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
import uuid
from pathlib import Path

from axosyslog_light.common.blocking import wait_until_true
from axosyslog_light.helpers.clickhouse.clickhouse_server_executor import CLICKHOUSE_OPTIONS_WITH_SCHEMA
from axosyslog_light.helpers.clickhouse.clickhouse_server_executor import ClickhouseServerTLS
from axosyslog_light.helpers.tls.certificate_generator import generate_tls_certificates
from axosyslog_light.syslog_ng_config.__init__ import stringify


def _run_tls_delivery(request, config, syslog_ng, clickhouse_server, clickhouse_ports, mutual):
    # The clickhouse() driver speaks gRPC over the grpc_port, so the server cert
    # must be verifiable against the connect address (127.0.0.1) via SAN.
    certs = generate_tls_certificates(Path.cwd())

    custom_input_msg = f"test message {str(uuid.uuid4())}"
    generator_source = config.create_example_msg_generator_source(num=1, template=f'{stringify(custom_input_msg)}')

    tls_options = {"ca-file": stringify(str(certs.ca_cert))}
    if mutual:
        tls_options.update({
            "key-file": stringify(str(certs.client_key)),
            "cert-file": stringify(str(certs.client_cert)),
        })

    clickhouse_options = CLICKHOUSE_OPTIONS_WITH_SCHEMA.copy()
    clickhouse_options.update({
        "url": f"'127.0.0.1:{clickhouse_ports.grpc_port}'",
        "auth": {"tls": tls_options},
    })
    clickhouse_destination = config.create_clickhouse_destination(**clickhouse_options)
    clickhouse_destination.create_clickhouse_client(clickhouse_ports.http_port)
    config.create_logpath(statements=[generator_source, clickhouse_destination])

    server_tls = ClickhouseServerTLS(
        cert_file=certs.server_cert,
        key_file=certs.server_key,
        ca_cert_file=certs.ca_cert if mutual else None,
        require_client_auth=mutual,
    )
    clickhouse_server.start(clickhouse_ports, tls=server_tls)
    clickhouse_destination.create_table(CLICKHOUSE_OPTIONS_WITH_SCHEMA["table"], [("message", "String")])
    request.addfinalizer(lambda: clickhouse_destination.delete_table())

    syslog_ng.start(config)

    assert wait_until_true(lambda: clickhouse_destination.get_stats()["written"] == 1)
    assert clickhouse_destination.get_stats()["dropped"] == 0
    assert clickhouse_destination.get_stats()["queued"] == 0
    assert clickhouse_destination.read_logs() == [{"message": custom_input_msg}]


def test_clickhouse_destination_grpc_tls(request, config, syslog_ng, clickhouse_server, clickhouse_ports):
    _run_tls_delivery(request, config, syslog_ng, clickhouse_server, clickhouse_ports, mutual=False)


def test_clickhouse_destination_grpc_mutual_tls(request, config, syslog_ng, clickhouse_server, clickhouse_ports):
    _run_tls_delivery(request, config, syslog_ng, clickhouse_server, clickhouse_ports, mutual=True)
