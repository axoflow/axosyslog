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
from concurrent.futures import ThreadPoolExecutor
from logging import getLogger
from threading import Event, Lock, Thread

import pytest

pytest.importorskip("botocore")

from botocore.exceptions import ClientError, EndpointConnectionError  # noqa: E402
from syslogng.modules.s3.s3_object import AlreadyFinishedError, S3Object, S3ObjectPersist  # noqa: E402

LOGGER = getLogger(__name__)
CHUNK_SIZE = S3Object.MIN_CHUNK_SIZE_BYTES

# A part upload must never wait for another task of the same pool.  Every wait below is bounded, so
# a regression fails the test instead of hanging the whole test run.
WAIT_TIMEOUT_SECONDS = 30


class FakeS3Client:
    """A boto3 S3 client stub that records the calls and injects part upload failures on demand."""

    def __init__(self, transient_failure_part=None, permanent_failure_part=None, failure_gate=None):
        self.transient_failure_part = transient_failure_part
        self.permanent_failure_part = permanent_failure_part
        self.failure_gate = failure_gate
        self.completed_parts = {}
        self.aborted_keys = []
        self.__failed_parts = set()
        self.__lock = Lock()

    def list_multipart_uploads(self, **kwargs):
        return {"IsTruncated": False}

    def list_objects(self, **kwargs):
        return {"IsTruncated": False}

    def create_multipart_upload(self, **kwargs):
        return {"UploadId": "upload-id-" + kwargs["Key"]}

    def upload_part(self, **kwargs):
        key = kwargs["Key"]
        part_number = kwargs["PartNumber"]

        with self.__lock:
            transient = part_number == self.transient_failure_part and (key, part_number) not in self.__failed_parts
            if transient:
                self.__failed_parts.add((key, part_number))

        if transient:
            if self.failure_gate is not None:
                self.failure_gate.wait(WAIT_TIMEOUT_SECONDS)
            raise EndpointConnectionError(endpoint_url="http://localhost")

        if part_number == self.permanent_failure_part:
            raise ClientError(
                {"Error": {"Code": "UnprocessableEntity"}, "ResponseMetadata": {"HTTPStatusCode": 422}},
                "UploadPart",
            )

        return {"ETag": "etag-%d" % part_number}

    def complete_multipart_upload(self, **kwargs):
        with self.__lock:
            self.completed_parts[kwargs["Key"]] = [part["PartNumber"] for part in kwargs["MultipartUpload"]["Parts"]]
        return {}

    def abort_multipart_upload(self, **kwargs):
        with self.__lock:
            self.aborted_keys.append(kwargs["Key"])
        return {}


def create_s3_object(working_dir, executor, client, exit_requested, index=0):
    return S3Object.create_initial(
        working_dir=working_dir / ("object-%d" % index),
        bucket="test-bucket",
        target_key="test-key-%d" % index,
        timestamp="",
        suffix=".log",
        compress=False,
        compresslevel=9,
        chunk_size=CHUNK_SIZE,
        server_side_encryption="",
        kms_key="",
        storage_class="STANDARD",
        canned_acl="",
        content_type="",
        persist_name="test-persist-%d" % index,
        executor=executor,
        client=client,
        logger=LOGGER,
        exit_requested=exit_requested,
    )


def write_parts(s3_object, number_of_parts):
    """Writes enough data to submit number_of_parts - 1 part uploads, plus a tail for finish()."""
    for _ in range(number_of_parts - 1):
        s3_object.write(b"x" * (CHUNK_SIZE + 1))
    s3_object.write(b"tail")


def wait_for_uploads(s3_objects, timeout=WAIT_TIMEOUT_SECONDS):
    """Returns False if the uploads did not settle in time, which is how a deadlock shows up."""
    finished = Event()

    def waiter():
        for s3_object in s3_objects:
            s3_object.wait_for_upload_to_complete()
        finished.set()

    Thread(target=waiter, daemon=True).start()
    return finished.wait(timeout)


def leftover_files(working_dir):
    return sorted(path.name for path in working_dir.rglob("*") if path.is_file())


def test_multipart_upload_of_a_single_object(tmp_path):
    client = FakeS3Client()
    s3_object = create_s3_object(tmp_path, ThreadPoolExecutor(max_workers=8), client, Event())
    write_parts(s3_object, 3)
    s3_object.finish()

    assert wait_for_uploads([s3_object])
    assert client.completed_parts == {"test-key-0.log": [1, 2, 3]}
    assert leftover_files(tmp_path) == []


def test_empty_object_is_completed(tmp_path):
    client = FakeS3Client()
    s3_object = create_s3_object(tmp_path, ThreadPoolExecutor(max_workers=8), client, Event())
    s3_object.finish()

    assert wait_for_uploads([s3_object])
    assert client.completed_parts == {"test-key-0.log": [1]}
    assert leftover_files(tmp_path) == []


def test_finish_after_a_chunk_rollover_closes_the_object(tmp_path):
    """Regression test: finish() cleared __prev_chunk only together with __current_chunk, so a
    finish() right after a chunk rollover left the object writable.  The next write() opened a part
    on it that was never uploaded."""
    client = FakeS3Client()
    s3_object = create_s3_object(tmp_path, ThreadPoolExecutor(max_workers=8), client, Event())

    s3_object.write(b"x" * (CHUNK_SIZE + 1))
    s3_object.finish()

    with pytest.raises(AlreadyFinishedError):
        s3_object.write(b"data after finish")

    assert wait_for_uploads([s3_object])
    assert client.completed_parts == {"test-key-0.log": [1]}
    assert leftover_files(tmp_path) == []


def test_transient_part_upload_failure_is_retried(tmp_path):
    client = FakeS3Client(transient_failure_part=2)
    s3_object = create_s3_object(tmp_path, ThreadPoolExecutor(max_workers=8), client, Event())
    write_parts(s3_object, 3)
    s3_object.finish()

    assert wait_for_uploads([s3_object])
    assert client.completed_parts == {"test-key-0.log": [1, 2, 3]}


def test_permanent_part_upload_failure_drops_the_part(tmp_path):
    client = FakeS3Client(permanent_failure_part=2)
    s3_object = create_s3_object(tmp_path, ThreadPoolExecutor(max_workers=8), client, Event())
    write_parts(s3_object, 3)
    s3_object.finish()

    assert wait_for_uploads([s3_object])
    assert client.completed_parts == {"test-key-0.log": [1, 3]}


def test_pending_part_of_a_loaded_object_is_uploaded(tmp_path):
    client = FakeS3Client()
    executor = ThreadPoolExecutor(max_workers=8)
    s3_object = create_s3_object(tmp_path, executor, client, Event())
    write_parts(s3_object, 2)
    executor.shutdown()

    persist_path = next((tmp_path / "object-0").rglob("*.json"))
    loaded = S3Object.load_finished(
        persist=S3ObjectPersist.load(path=persist_path),
        executor=ThreadPoolExecutor(max_workers=8),
        client=client,
        logger=LOGGER,
        exit_requested=Event(),
    )

    assert wait_for_uploads([loaded])
    assert client.completed_parts == {"test-key-0.log": [1, 2]}


def test_completion_does_not_wait_for_a_queued_retry(tmp_path):
    """Regression test: the completion task used to block a worker while waiting for a part upload
    retry that was queued behind it.  With a single upload thread that retry never got a worker."""
    client = FakeS3Client(transient_failure_part=2)
    s3_object = create_s3_object(tmp_path, ThreadPoolExecutor(max_workers=1), client, Event())
    write_parts(s3_object, 3)
    s3_object.finish()

    assert wait_for_uploads([s3_object]), "the completion task deadlocked the single upload thread"
    assert client.completed_parts == {"test-key-0.log": [1, 2, 3]}


def test_completion_does_not_starve_the_upload_pool(tmp_path):
    """Regression test: one blocked completion task per worker starved the whole pool, so no part
    upload retry could run any more.  The gate holds every failure until all objects are finished."""
    failure_gate = Event()
    client = FakeS3Client(transient_failure_part=2, failure_gate=failure_gate)
    executor = ThreadPoolExecutor(max_workers=8)
    exit_requested = Event()

    s3_objects = [create_s3_object(tmp_path, executor, client, exit_requested, index) for index in range(8)]
    for s3_object in s3_objects:
        write_parts(s3_object, 3)
    for s3_object in s3_objects:
        s3_object.finish()
    failure_gate.set()

    assert wait_for_uploads(s3_objects), "the completion tasks starved the upload pool"
    assert len(client.completed_parts) == 8
    assert all(parts == [1, 2, 3] for parts in client.completed_parts.values())


def test_exit_request_abandons_the_object_and_keeps_the_persist(tmp_path):
    """On exit the part upload retries are skipped, so the object stays on disk for the next start."""
    failure_gate = Event()
    client = FakeS3Client(transient_failure_part=2, failure_gate=failure_gate)
    exit_requested = Event()
    s3_object = create_s3_object(tmp_path, ThreadPoolExecutor(max_workers=8), client, exit_requested)

    write_parts(s3_object, 2)
    exit_requested.set()
    failure_gate.set()
    s3_object.finish()

    assert wait_for_uploads([s3_object]), "wait_for_upload_to_complete() did not return after the exit request"
    assert client.completed_parts == {}
    assert len(leftover_files(tmp_path)) > 0
