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

import pytest
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


@pytest.mark.skip(reason="long-running test, only run manually")
def test_s3_destination_stops_after_losing_the_server(config, syslog_ng, s3_server):
    # Regression test: the multipart completion task ran on the upload pool and waited there for a
    # part upload retry that was queued behind it.  With upload_threads(1) that retry never got a
    # worker, so the pool was starved and syslog-ng could not stop any more.
    #
    # The flush timer is what makes this reproducible.  It finishes the object while the destination
    # is still running, so the failed part upload is retried instead of being skipped by the exit
    # request that a shutdown raises first.  flush_grace_period() counts in minutes and its smallest
    # accepted value is 1, and the flush poll runs once a minute, so the object is finished at most
    # about two minutes after the last write.
    message = f"test message {uuid.uuid4()}"
    generator_source = _generator_source(config, message, num=3)
    s3_destination = config.create_s3_destination(
        s3_server,
        bucket=BUCKET,
        object_key=OBJECT_KEY,
        template="$MSG\n",
        upload_threads=1,
        flush_grace_period=1,
    )
    config.add_include("scl.conf")
    config.create_logpath(statements=[generator_source, s3_destination])

    syslog_ng.start(config, startup_timeout=60)
    assert wait_until_true_custom(
        lambda: s3_destination.get_stats().get("written", 0) == 3,
        timeout=60,
    ), "timed out waiting for 3 messages to be written to the destination"

    # Every S3 call fails from here on, so the part upload started by the flush timer is retried.
    s3_server.stop()
    s3_server.process = None  # the fixture must not terminate the process a second time

    assert wait_until_true_custom(
        lambda: syslog_ng.is_message_in_console_log("Failed to create multipart upload"),
        timeout=180,
    ), "timed out waiting for the flush timer to finish the object"

    # A starved upload pool used to keep syslog-ng alive here.
    syslog_ng.stop()
