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
from concurrent.futures import ThreadPoolExecutor

from axosyslog_light.common.file import copy_shared_file

MESSAGES_PER_SENDER = 99
DATAGRAM_RECEIVE_BUFFER_BYTES = 131072
TEMPLATE = r'"${MSG}\n"'


def _bsd_messages(tag):
    return [
        "<7>2004-09-07T10:43:21+01:00 bzorp prog[12345]: {} {:05d}".format(tag, seq)
        for seq in range(1, MESSAGES_PER_SENDER + 1)
    ]


def _rfc5424_messages(tag):
    return [
        "<7>1 2004-09-07T10:43:21+01:00 bzorp prog 12345 - - {} {:05d}".format(tag, seq)
        for seq in range(1, MESSAGES_PER_SENDER + 1)
    ]


def _build_senders(config, port_allocator, testcase_parameters):
    tls_options = {
        "key-file": copy_shared_file(testcase_parameters, "server.key"),
        "cert-file": copy_shared_file(testcase_parameters, "server.crt"),
        "peer-verify": "optional-untrusted",
    }

    unix_dgram = config.create_unix_dgram_source(path="input-dgram.sock", flags="expect-hostname")
    unix_stream = config.create_unix_stream_source(path="input-stream.sock", flags="expect-hostname")
    pipe = config.create_pipe_source(file_name="input.pipe", flags="expect-hostname")
    padded_pipe = config.create_pipe_source(file_name="input-padded.pipe", pad_size=2048, flags="expect-hostname")
    file_source = config.create_file_source(file_name="input.log")
    network_tcp = config.create_network_source(ip="localhost", port=port_allocator(), transport="tcp")
    network_tls = config.create_network_source(ip="localhost", port=port_allocator(), transport="tls", tls=tls_options)
    network_udp = config.create_network_source(ip="localhost", port=port_allocator(), transport="udp", so_rcvbuf=DATAGRAM_RECEIVE_BUFFER_BYTES)
    syslog_tcp = config.create_syslog_source(ip="localhost", port=port_allocator(), transport="tcp")
    syslog_udp = config.create_syslog_source(ip="localhost", port=port_allocator(), transport="udp", so_rcvbuf=DATAGRAM_RECEIVE_BUFFER_BYTES)

    return {
        "unix-dgram": (unix_dgram, lambda tag: unix_dgram.write_logs([m + "\n" for m in _bsd_messages(tag)])),
        "unix-stream": (unix_stream, lambda tag: unix_stream.write_logs(_bsd_messages(tag))),
        "pipe": (pipe, lambda tag: pipe.write_logs(_bsd_messages(tag))),
        "padded-pipe": (padded_pipe, lambda tag: padded_pipe.write_logs(_bsd_messages(tag))),
        "file": (file_source, lambda tag: file_source.write_logs(_bsd_messages(tag))),
        "network-tcp": (network_tcp, lambda tag: network_tcp.write_logs(_bsd_messages(tag))),
        "network-tls": (network_tls, lambda tag: network_tls.write_logs(_bsd_messages(tag))),
        "network-udp": (network_udp, lambda tag: network_udp.write_logs(_bsd_messages(tag))),
        "syslog-tcp": (syslog_tcp, lambda tag: syslog_tcp.write_logs(_rfc5424_messages(tag), framed=True)),
        "syslog-udp": (syslog_udp, lambda tag: syslog_udp.write_logs(_rfc5424_messages(tag), framed=False)),
    }


def _group_sequences_by_tag(lines, tags):
    sequences = {tag: [] for tag in tags}
    for line in lines:
        tag, sequence_number = line.rsplit(" ", 1)
        sequences[tag].append(int(sequence_number))
    return sequences


def test_concurrent_senders_deliver_every_message_in_order(config, syslog_ng, port_allocator, testcase_parameters):
    config.update_global_options(ts_format="iso", keep_hostname="yes", chain_hostnames="no")

    senders = _build_senders(config, port_allocator, testcase_parameters)
    file_destination = config.create_file_destination(file_name="output.log", template=TEMPLATE)
    config.create_logpath(statements=[source for source, _ in senders.values()] + [file_destination])

    syslog_ng.start(config)

    with ThreadPoolExecutor(max_workers=len(senders)) as executor:
        for tag, (_, send_messages) in senders.items():
            executor.submit(send_messages, tag)

    lines = file_destination.read_logs(len(senders) * MESSAGES_PER_SENDER)

    sequences = _group_sequences_by_tag(lines, senders.keys())
    for tag in senders:
        assert sequences[tag] == list(range(1, MESSAGES_PER_SENDER + 1)), (
            "{}: expected {} messages 1..{} in order, got {}".format(
                tag, MESSAGES_PER_SENDER, MESSAGES_PER_SENDER, sequences[tag],
            )
        )
