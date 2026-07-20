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
import uuid

from axosyslog_light.common.blocking import wait_until_true_custom

BUCKET = "test-bucket"
OBJECT_KEY = "test-object"


def _generator_source(config, message, num):
    return config.create_example_msg_generator_source(
        num=num,
        freq=0,
        template=config.stringify(message),
    )


def _run_and_flush(syslog_ng, config, s3_destination, num):
    # the ASan binary importing boto3 during config init regularly overruns the default
    # startup timeout on a loaded CI runner
    syslog_ng.start(config, startup_timeout=60)
    assert wait_until_true_custom(
        lambda: s3_destination.get_stats().get("written", 0) == num,
        timeout=60,
    ), f"timed out waiting for {num} messages to be written to the destination"
    # A graceful stop finishes every open object and waits for the uploads to complete, so the
    # buffered messages end up in S3.
    syslog_ng.stop()


def test_s3_destination_roundtrip(config, syslog_ng, s3_server):
    message = f"test message {uuid.uuid4()}"
    generator_source = _generator_source(config, message, num=3)
    s3_destination = config.create_s3_destination(
        s3_server,
        bucket=BUCKET,
        object_key=OBJECT_KEY,
        template="$MSG\n",
    )
    config.add_include("scl.conf")
    config.create_logpath(statements=[generator_source, s3_destination])

    _run_and_flush(syslog_ng, config, s3_destination, num=3)

    assert s3_destination.list_object_keys() == [OBJECT_KEY]
    assert s3_destination.read_object_lines(OBJECT_KEY) == [message, message, message]


def test_s3_destination_compression(config, syslog_ng, s3_server):
    message = f"test message {uuid.uuid4()}"
    generator_source = _generator_source(config, message, num=3)
    s3_destination = config.create_s3_destination(
        s3_server,
        bucket=BUCKET,
        object_key=OBJECT_KEY,
        template="$MSG\n",
        compression=True,
    )
    config.add_include("scl.conf")
    config.create_logpath(statements=[generator_source, s3_destination])

    _run_and_flush(syslog_ng, config, s3_destination, num=3)

    assert s3_destination.list_object_keys() == [f"{OBJECT_KEY}.gz"]
    assert s3_destination.read_object_lines(f"{OBJECT_KEY}.gz") == [message, message, message]


def test_s3_destination_canned_acl(config, syslog_ng, s3_server):
    # Regression test: canned_acl() used to reach the S3 API as a tuple and raise an unhandled
    # exception, dropping the object.
    message = f"test message {uuid.uuid4()}"
    generator_source = _generator_source(config, message, num=1)
    s3_destination = config.create_s3_destination(
        s3_server,
        bucket=BUCKET,
        object_key=OBJECT_KEY,
        template="$MSG\n",
        canned_acl="private",
    )
    config.add_include("scl.conf")
    config.create_logpath(statements=[generator_source, s3_destination])

    _run_and_flush(syslog_ng, config, s3_destination, num=1)

    assert s3_destination.list_object_keys() == [OBJECT_KEY]
    assert s3_destination.read_object_lines(OBJECT_KEY) == [message]
