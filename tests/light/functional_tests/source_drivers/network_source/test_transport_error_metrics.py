#!/usr/bin/env python
#############################################################################
# Copyright (c) 2026 Axoflow
# Copyright (c) 2026 Szilard Parrag <szilard.parrag@axoflow.com>
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

from axosyslog_light.common.blocking import wait_until_true
from axosyslog_light.common.file import copy_shared_file
from axosyslog_light.syslog_ng_ctl.prometheus_stats_handler import MetricFilter


METRIC_NAME = "syslogng_input_transport_errors_total"


def _get_samples(config, reason):
    return config.get_prometheus_samples([MetricFilter(METRIC_NAME, {"reason": reason})])


def _get_metric_value(config, reason):
    return sum(s.value for s in _get_samples(config, reason))


def test_invalid_frame_header_metric(config, syslog_ng, port_allocator, testcase_parameters):
    config.update_global_options(stats_level=1)

    source = config.create_syslog_source(
        ip="localhost",
        port=port_allocator(),
        transport="tcp",
    )
    file_destination = config.create_file_destination(file_name="output.log")
    config.create_logpath(statements=[source, file_destination])

    syslog_ng.start(config)

    # Send raw data that is not a valid RFC6587 frame header (starts with a letter, not a digit)
    msg_to_send = "INVALID_FRAME_HEADER\n"
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect(("localhost", source.options["port"]))
    sock.sendall(bytes(msg_to_send, "utf-8"))
    sock.close()

    assert wait_until_true(lambda: _get_metric_value(config, "invalid-frame-header") == 1)
    syslog_ng.wait_for_message_in_console_log(
        f"Invalid frame header; header='{msg_to_send[:10]}'",
    )


def test_tls_handshake_error_metric(config, syslog_ng, port_allocator, testcase_parameters):
    config.update_global_options(stats_level=1)

    server_key_path = copy_shared_file(testcase_parameters, "server.key")
    server_cert_path = copy_shared_file(testcase_parameters, "server.crt")

    source = config.create_network_source(
        ip="localhost",
        port=port_allocator(),
        transport="tls",
        tls={
            "key-file": server_key_path,
            "cert-file": server_cert_path,
            "peer-verify": "optional-untrusted",
        },
    )
    file_destination = config.create_file_destination(file_name="output.log")
    config.create_logpath(statements=[source, file_destination])

    syslog_ng.start(config)

    # Connect with plain TCP (no TLS handshake) to trigger a TLS handshake error
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect(("localhost", source.options["port"]))
    sock.sendall(b"THIS IS NOT A TLS HANDSHAKE\n")
    sock.close()

    assert wait_until_true(lambda: _get_metric_value(config, "tls-handshake") == 1)
    assert syslog_ng.wait_for_message_in_console_log("SSL error while reading stream") != []

    samples = _get_samples(config, "tls-handshake")
    assert len(samples) == 1
    assert samples[0].labels["tls_error"] == "0A00010B"
    assert samples[0].labels["tls_error_string"] == "SSL routines::wrong version number"


def test_tls_wrong_client_cert_metric(config, syslog_ng, port_allocator, testcase_parameters):
    config.update_global_options(stats_level=1)

    server_key_path = copy_shared_file(testcase_parameters, "server.key")
    server_cert_path = copy_shared_file(testcase_parameters, "server.crt")
    trusted_ca_path = copy_shared_file(testcase_parameters, "valid-ca.crt")

    source = config.create_network_source(
        ip="localhost",
        port=port_allocator(),
        transport="tls",
        tls={
            "key-file": server_key_path,
            "cert-file": server_cert_path,
            "ca-file": trusted_ca_path,
            "peer-verify": "required-trusted",
        },
    )
    file_destination = config.create_file_destination(file_name="output.log")
    config.create_logpath(statements=[source, file_destination])

    syslog_ng.start(config)

    # TLS connection without  client cert, server requires one and will reject
    client_ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    client_ctx.check_hostname = False
    client_ctx.verify_mode = ssl.CERT_NONE

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect(("localhost", source.options["port"]))
    try:
        client_ctx.wrap_socket(sock, server_hostname="localhost")
    except ssl.SSLError:  # This is intentional
        pass
    finally:
        sock.close()

    assert wait_until_true(lambda: _get_metric_value(config, "tls-handshake") == 1)
    assert syslog_ng.wait_for_message_in_console_log("SSL error while reading stream") != []

    samples = _get_samples(config, "tls-handshake")
    assert len(samples) == 1
    assert samples[0].labels["tls_error"] == "0A0000C7"
    assert samples[0].labels["tls_error_string"] == "SSL routines::peer did not return a certificate"
