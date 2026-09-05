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
import logging
import sys

import boto3
from axosyslog_light.executors.process_executor import ProcessExecutor
from botocore.config import Config
from tenacity import retry
from tenacity import stop_after_delay
from tenacity import wait_fixed

logger = logging.getLogger(__name__)

# Each probe attempt must fail well inside the tenacity stop_after_delay() bound below.
S3_SERVER_PROBE_CONFIG = Config(retries={"max_attempts": 0}, connect_timeout=1, read_timeout=2)

# Dummy credentials; the mock server accepts anything.
S3_SERVER_OPTIONS = {
    "access_key": "axosyslog-light",
    "secret_key": "axosyslog-light",
    "region": "us-east-1",
}


class S3ServerExecutor():
    """A local, S3-compatible mock server backed by moto, started as a subprocess.

    The s3() destination talks to it over HTTP via its url() option, exactly as it would to a
    real S3 endpoint, so the destination's real boto3 code path (multipart uploads included) is
    exercised end-to-end without any AWS access.
    """

    def __init__(self) -> None:
        self.process = None
        self.endpoint_url = None
        self.access_key = S3_SERVER_OPTIONS["access_key"]
        self.secret_key = S3_SERVER_OPTIONS["secret_key"]
        self.region = S3_SERVER_OPTIONS["region"]

    def start(self, port: int) -> None:
        self.endpoint_url = f"http://127.0.0.1:{port}"
        command = [sys.executable, "-m", "moto.server", "-H", "127.0.0.1", "-p", str(port)]
        self.process = ProcessExecutor().start(command, "s3_server.stdout", "s3_server.stderr")
        self.wait_for_server_start()
        logger.info("S3 mock server started with PID: %s on %s", self.process.pid, self.endpoint_url)

    def stop(self) -> None:
        self.process.terminate()
        self.process.wait(timeout=10)
        logger.info("S3 mock server with PID %s terminated.", self.process.pid)

    @retry(wait=wait_fixed(0.1), reraise=True, stop=stop_after_delay(30))
    def wait_for_server_start(self) -> None:
        client = boto3.client(
            "s3",
            endpoint_url=self.endpoint_url,
            region_name=S3_SERVER_OPTIONS["region"],
            aws_access_key_id=S3_SERVER_OPTIONS["access_key"],
            aws_secret_access_key=S3_SERVER_OPTIONS["secret_key"],
            config=S3_SERVER_PROBE_CONFIG,
        )
        try:
            client.list_buckets()
        except Exception as e:
            raise RuntimeError("S3 mock server is not ready yet.") from e
