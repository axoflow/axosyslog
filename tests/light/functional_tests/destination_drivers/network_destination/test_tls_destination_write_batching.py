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
from axosyslog_light.common.file import copy_shared_file


def _build_tls_destination(config, port_allocator, testcase_parameters, **extra_options):
    ca = copy_shared_file(testcase_parameters, "valid-ca.crt")
    tls_destination = config.create_network_destination(
        ip="localhost", port=port_allocator(), transport="tls",
        tls={
            "ca-file": ca,
            "peer-verify": "yes",
        },
        **extra_options,
    )
    return tls_destination


def _messages(count):
    # zero-padded index so order is checkable; payload long enough to span several TLS records
    return [f"tls-batch-seq-{i:06d}-{'x' * 200}" for i in range(count)]


def _assert_received_in_order(received, sent):
    assert len(received) == len(sent), f"expected {len(sent)} messages, got {len(received)}"
    for got, expected in zip(received, sent):
        token = expected.split("-x")[0]
        assert token in got, f"out-of-order or corrupted: expected {token!r} in {got!r}"


def test_tls_destination_batches_preserve_all_messages(config, syslog_ng, port_allocator, testcase_parameters):
    """@version 4.26: many messages arrive intact and in order across many batches."""
    config.set_version("4.26")

    network_source = config.create_network_source(port=port_allocator())
    tls_destination = _build_tls_destination(config, port_allocator, testcase_parameters)
    config.create_logpath(statements=[network_source, tls_destination])

    tls_destination.start_listener()
    syslog_ng.start(config)

    sent = _messages(1000)
    network_source.write_logs(sent)

    received = tls_destination.read_logs(len(sent))
    _assert_received_in_order(received, sent)


def test_tls_destination_explicit_flush_lines(config, syslog_ng, port_allocator, testcase_parameters):
    """Explicit flush-lines() sets the batch size; send full batches plus a tail."""
    config.set_version("4.26")

    network_source = config.create_network_source(port=port_allocator())
    tls_destination = _build_tls_destination(config, port_allocator, testcase_parameters, flush_lines=50)
    config.create_logpath(statements=[network_source, tls_destination])

    tls_destination.start_listener()
    syslog_ng.start(config)

    sent = _messages(525)  # 10 full batches of 50 + a 25-message tail
    network_source.write_logs(sent)

    received = tls_destination.read_logs(len(sent))
    _assert_received_in_order(received, sent)


def test_tls_destination_single_message_is_flushed(config, syslog_ng, port_allocator, testcase_parameters):
    """A lone message must not be stranded waiting for the batch to fill."""
    config.set_version("4.26")

    network_source = config.create_network_source(port=port_allocator())
    tls_destination = _build_tls_destination(config, port_allocator, testcase_parameters)
    config.create_logpath(statements=[network_source, tls_destination])

    tls_destination.start_listener()
    syslog_ng.start(config)

    network_source.write_log("tls-batch-single")
    assert tls_destination.read_until_logs(["tls-batch-single"])


def test_tls_destination_no_batch_old_version_is_correct(config, syslog_ng, port_allocator, testcase_parameters):
    """@version 4.0: the version gate keeps the non-batched path; output still correct."""
    config.set_version("4.0")

    network_source = config.create_network_source(port=port_allocator())
    tls_destination = _build_tls_destination(config, port_allocator, testcase_parameters)
    config.create_logpath(statements=[network_source, tls_destination])

    tls_destination.start_listener()
    syslog_ng.start(config)

    sent = _messages(200)
    network_source.write_logs(sent)

    received = tls_destination.read_logs(len(sent))
    _assert_received_in_order(received, sent)
