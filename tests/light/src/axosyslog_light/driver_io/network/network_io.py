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
import socket
import ssl
from enum import Enum
from enum import IntEnum
from pathlib import Path

from axosyslog_light.common.asynchronous import BackgroundEventLoop
from axosyslog_light.common.blocking import DEFAULT_TIMEOUT
from axosyslog_light.common.file import File
from axosyslog_light.common.file import get_shared_file
from axosyslog_light.common.network import SingleConnectionTCPServer
from axosyslog_light.common.network import UDPServer
from axosyslog_light.common.random_id import get_unique_id
from axosyslog_light.driver_io import message_readers
from axosyslog_light.helpers.loggen.loggen import Loggen
from axosyslog_light.helpers.loggen.loggen import LoggenStartParams


class NetworkIO():
    def __init__(self, ip, port, transport, framed, ip_proto_version=None):
        self.__ip = ip
        self.__port = port
        self.__transport = transport
        self.__framed = framed
        self.__ip_proto_version = NetworkIO.IPProtoVersion.V4 if ip_proto_version is None else ip_proto_version
        self.__server = None
        self.__message_reader = None

    def write_raw(self, raw_content, rate=None, transport=None, extra_loggen_args=None):
        if transport is None:
            transport = self.__transport

        if extra_loggen_args is None:
            extra_loggen_args = {}

        loggen_input_file_path = Path("loggen_input_{}.txt".format(get_unique_id()))

        loggen_input_file = File(loggen_input_file_path)
        loggen_input_file.write_content_and_close(raw_content)

        Loggen().start(
            LoggenStartParams(
                target=self.__ip,
                port=self.__port,
                read_file=str(loggen_input_file_path),
                dont_parse=True,
                permanent=True,
                rate=rate,
                **transport.value,
                **extra_loggen_args,
            ),
        )

    def __format_messages(self, messages, framed=None):
        if framed is None:
            framed = self.__framed
        if framed:
            return "".join([str(len(message)) + " " + message for message in messages])
        return "".join([message + "\n" for message in messages])

    def write_messages(self, messages, rate=None, transport=None, framed=None):
        self.write_raw(self.__format_messages(messages, framed=framed), rate=rate, transport=transport)

    def write_messages_with_proxy_header(self, proxy_version, src_ip, dst_ip, src_port, dst_port, messages, rate=None, transport=None, framed=None):
        self.write_raw(
            self.__format_messages(messages, framed=framed),
            rate=rate,
            transport=transport,
            extra_loggen_args={
                "proxied": proxy_version,
                "proxy_src_ip": src_ip,
                "proxy_dst_ip": dst_ip,
                "proxy_src_port": str(src_port),
                "proxy_dst_port": str(dst_port),
            },
        )

    def start_listener(self):
        self.__message_reader, self.__server = self.__transport.construct(self.__framed, self.__port, self.__ip, self.__ip_proto_version)
        BackgroundEventLoop().wait_async_result(self.__server.start(), timeout=DEFAULT_TIMEOUT)

    def stop_listener(self):
        if self.__message_reader is not None:
            BackgroundEventLoop().wait_async_result(self.__server.stop(), timeout=DEFAULT_TIMEOUT)
            self.__message_reader = None
            self.__server = None

    def read_number_of_messages(self, counter, timeout=DEFAULT_TIMEOUT):
        return self.__message_reader.wait_for_number_of_messages(counter, timeout)

    def read_until_messages(self, lines, timeout=DEFAULT_TIMEOUT):
        return self.__message_reader.wait_for_messages(lines, timeout)

    class IPProtoVersion(IntEnum):
        V4 = socket.AF_INET
        V6 = socket.AF_INET6

    class Transport(Enum):
        AUTO = {}
        TCP = {"inet": True, "stream": True}
        UDP = {"dgram": True}
        TLS = {"use_ssl": True}
        PROXIED_TCP = {"inet": True, "stream": True}
        PROXIED_TLS = {"use_ssl": True}
        PROXIED_TLS_PASSTHROUGH = {"use_ssl": True, "proxied_tls_passthrough": True}

        def construct(self, framed, port, host=None, ip_proto_version=None):
            def _construct(server, reader_class):
                return reader_class(server), server

            tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            tls.load_cert_chain(get_shared_file("valid-localhost.crt"), get_shared_file("valid-localhost.key"))

            transport_mapping = {
                NetworkIO.Transport.TCP: lambda: _construct(SingleConnectionTCPServer(port, host, ip_proto_version), None if framed else message_readers.SingleLineStreamReader),
                NetworkIO.Transport.TLS: lambda: _construct(SingleConnectionTCPServer(port, host, ip_proto_version, tls), None if framed else message_readers.SingleLineStreamReader),
                NetworkIO.Transport.UDP: lambda: _construct(UDPServer(port, host, ip_proto_version), message_readers.DatagramReader),
            }
            return transport_mapping[self]()
