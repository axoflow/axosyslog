#!/usr/bin/env python
#############################################################################
# Copyright (c) 2020 One Identity
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as published
# by the Free Software Foundation, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
#
# As an additional exemption you are allowed to compile & link against the
# OpenSSL libraries as published by the OpenSSL project. See the file
# COPYING for details.
#
#############################################################################
from axosyslog_light.common.file import copy_shared_file
from axosyslog_light.driver_io.network.network_io import NetworkIO


def _test_network_transports(
        config,
        syslog_ng,
        syslog_ng_ctl,
        port_allocator,
        testcase_parameters,
        source_transport,
        framed=None,
        client_transport=None,
        password=None,
):
    TEMPLATE = r'"${SOURCEIP} ${SOURCEPORT} ${DESTIP} ${DESTPORT} ${IP_PROTO} ${MESSAGE}\n"'
    PROXY_VERSION = 1
    PROXY_SRC_IP = "1.1.1.1"
    PROXY_DST_IP = "2.2.2.2"
    PROXY_SRC_PORT = 3333
    PROXY_DST_PORT = 4444
    EXPECTED_PROXIED_MESSAGE = "1.1.1.1 3333 2.2.2.2 4444 4 message 0"
    INPUT_MESSAGE = "message 0"
    OUTPUT_FILE = "output.log"

    extra_opts = {}
    if "tls" in source_transport or (client_transport is not None and "use_ssl" in client_transport.value):
        if password:
            server_key_path = copy_shared_file(testcase_parameters, "server-protected-asdfg.key")
            server_cert_path = copy_shared_file(testcase_parameters, "server-protected-asdfg.crt")
        else:
            server_key_path = copy_shared_file(testcase_parameters, "server.key")
            server_cert_path = copy_shared_file(testcase_parameters, "server.crt")

        extra_opts["tls"] = {
            "key-file": server_key_path,
            "cert-file": server_cert_path,
            "peer-verify": "optional-untrusted",
        }

    source = config.create_network_source(
        ip="localhost",
        port=port_allocator(),
        transport=source_transport,
        flags="no-parse",
        **extra_opts,
    )

    file_destination = config.create_file_destination(file_name=OUTPUT_FILE, template=TEMPLATE)
    config.create_logpath(statements=[source, file_destination])

    syslog_ng.start(config)
    if password is not None:
        syslog_ng_ctl.credentials_add(credential=server_key_path, secret=password)

    if "proxied" in source_transport:
        source.write_logs_with_proxy_header(
            PROXY_VERSION,
            PROXY_SRC_IP,
            PROXY_DST_IP,
            PROXY_SRC_PORT,
            PROXY_DST_PORT,
            [INPUT_MESSAGE],
            transport=client_transport,
            framed=framed,
        )
        assert file_destination.read_log() == EXPECTED_PROXIED_MESSAGE
    else:
        source.write_log(INPUT_MESSAGE, transport=client_transport)
        assert file_destination.read_log().endswith(INPUT_MESSAGE)


def test_pp_network_tcp(config, syslog_ng, syslog_ng_ctl, port_allocator, testcase_parameters):
    _test_network_transports(config, syslog_ng, syslog_ng_ctl, port_allocator, testcase_parameters, "proxied-tcp")


def test_pp_network_tls(config, syslog_ng, syslog_ng_ctl, port_allocator, testcase_parameters):
    _test_network_transports(config, syslog_ng, syslog_ng_ctl, port_allocator, testcase_parameters, "proxied-tls")


def test_pp_network_tls_with_passphrase(config, syslog_ng, syslog_ng_ctl, port_allocator, testcase_parameters):
    _test_network_transports(config, syslog_ng, syslog_ng_ctl, port_allocator, testcase_parameters, "proxied-tls", password="asdfg")


def test_pp_network_tls_passthrough(config, syslog_ng, syslog_ng_ctl, port_allocator, testcase_parameters):
    _test_network_transports(config, syslog_ng, syslog_ng_ctl, port_allocator, testcase_parameters, "proxied-tls-passthrough")


def test_network_tcp(config, syslog_ng, syslog_ng_ctl, port_allocator, testcase_parameters):
    _test_network_transports(config, syslog_ng, syslog_ng_ctl, port_allocator, testcase_parameters, "tcp")


def test_network_tls(config, syslog_ng, syslog_ng_ctl, port_allocator, testcase_parameters):
    _test_network_transports(config, syslog_ng, syslog_ng_ctl, port_allocator, testcase_parameters, "tls")


def test_network_auto_tcp_no_framing(config, syslog_ng, syslog_ng_ctl, port_allocator, testcase_parameters):
    _test_network_transports(config, syslog_ng, syslog_ng_ctl, port_allocator, testcase_parameters, "auto", framed=False, client_transport=NetworkIO.Transport.TCP)


def test_network_auto_tcp_w_framing(config, syslog_ng, syslog_ng_ctl, port_allocator, testcase_parameters):
    _test_network_transports(config, syslog_ng, syslog_ng_ctl, port_allocator, testcase_parameters, "auto", framed=True, client_transport=NetworkIO.Transport.TCP)


def test_network_auto_tls_no_framing(config, syslog_ng, syslog_ng_ctl, port_allocator, testcase_parameters):
    _test_network_transports(config, syslog_ng, syslog_ng_ctl, port_allocator, testcase_parameters, "auto", framed=False, client_transport=NetworkIO.Transport.TLS)


def test_network_auto_tls_w_framing(config, syslog_ng, syslog_ng_ctl, port_allocator, testcase_parameters):
    _test_network_transports(config, syslog_ng, syslog_ng_ctl, port_allocator, testcase_parameters, "auto", framed=True, client_transport=NetworkIO.Transport.TLS)
