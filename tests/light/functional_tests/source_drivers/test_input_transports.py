#!/usr/bin/env python
#############################################################################
# Copyright (c) 2026 Axoflow
# Copyright (c) 2026 Attila Szakacs-Bertok <attila.szakacs@axoflow.com>
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
import pytest
from axosyslog_light.common.file import copy_shared_file

BSD_MESSAGE = "<7>2004-09-07T10:43:21+01:00 bzorp prog[12345]: message 0"
RFC5424_MESSAGE = "<7>1 2004-09-07T10:43:21+01:00 bzorp prog 12345 - - message 0"
EXPECTED_MESSAGE = "2004-09-07T10:43:21+01:00 bzorp prog 12345 message 0"
TEMPLATE = r'"${ISODATE} ${HOST} ${PROGRAM} ${PID} ${MSG}\n"'


def _unix_dgram(terminator):
    def build(config, port_allocator, testcase_parameters):
        source = config.create_unix_dgram_source(path="input-dgram.sock", flags="expect-hostname")
        return source, lambda: source.write_log(BSD_MESSAGE + terminator)
    return build


def _unix_stream():
    def build(config, port_allocator, testcase_parameters):
        source = config.create_unix_stream_source(path="input-stream.sock", flags="expect-hostname")
        return source, lambda: source.write_log(BSD_MESSAGE)
    return build


def _pipe(pad_size=None):
    def build(config, port_allocator, testcase_parameters):
        options = {"flags": "expect-hostname"}
        if pad_size is not None:
            options["pad_size"] = pad_size
        source = config.create_pipe_source(file_name="input.pipe", **options)
        return source, lambda: source.write_log(BSD_MESSAGE)
    return build


def _file():
    def build(config, port_allocator, testcase_parameters):
        source = config.create_file_source(file_name="input.log")
        return source, lambda: source.write_log(BSD_MESSAGE)
    return build


def _network(transport, tls=False):
    def build(config, port_allocator, testcase_parameters):
        extra_options = {}
        if tls:
            extra_options["tls"] = {
                "key-file": copy_shared_file(testcase_parameters, "server.key"),
                "cert-file": copy_shared_file(testcase_parameters, "server.crt"),
                "peer-verify": "optional-untrusted",
            }
        source = config.create_network_source(
            ip="localhost", port=port_allocator(), transport=transport, **extra_options,
        )
        return source, lambda: source.write_log(BSD_MESSAGE)
    return build


def _syslog(transport):
    def build(config, port_allocator, testcase_parameters):
        source = config.create_syslog_source(ip="localhost", port=port_allocator(), transport=transport)
        framed = transport == "tcp"
        return source, lambda: source.write_log(RFC5424_MESSAGE, framed=framed)
    return build


@pytest.mark.parametrize(
    "build_source",
    [
        pytest.param(_unix_dgram("\n"), id="unix-dgram-lf"),
        pytest.param(_unix_dgram("\0"), id="unix-dgram-nul"),
        pytest.param(_unix_dgram("\0\n"), id="unix-dgram-nul-lf"),
        pytest.param(_unix_dgram(""), id="unix-dgram-unterminated"),
        pytest.param(_unix_stream(), id="unix-stream"),
        pytest.param(_pipe(), id="pipe"),
        pytest.param(_pipe(pad_size=2048), id="pipe-padded"),
        pytest.param(_file(), id="file"),
        pytest.param(_network("tcp"), id="network-tcp"),
        pytest.param(_network("udp"), id="network-udp"),
        pytest.param(_network("tls", tls=True), id="network-tls"),
        pytest.param(_syslog("tcp"), id="syslog-tcp"),
        pytest.param(_syslog("udp"), id="syslog-udp"),
    ],
)
def test_input_transports(config, syslog_ng, port_allocator, testcase_parameters, build_source):
    config.update_global_options(ts_format="iso", keep_hostname="yes", chain_hostnames="no")

    source, send_message = build_source(config, port_allocator, testcase_parameters)
    file_destination = config.create_file_destination(file_name="output.log", template=TEMPLATE)
    config.create_logpath(statements=[source, file_destination])

    syslog_ng.start(config)
    send_message()

    assert file_destination.read_log() == EXPECTED_MESSAGE
