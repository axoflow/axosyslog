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
import gzip
from typing import List

import boto3
from botocore.config import Config


class S3Client:
    """boto3 client used by the tests to read back what the s3() destination uploaded."""

    def __init__(self, endpoint_url: str, access_key: str, secret_key: str, region: str) -> None:
        self.client = boto3.client(
            "s3",
            endpoint_url=endpoint_url,
            region_name=region,
            aws_access_key_id=access_key,
            aws_secret_access_key=secret_key,
            config=Config(retries={"max_attempts": 0}),
        )

    def list_object_keys(self, bucket: str) -> List[str]:
        response = self.client.list_objects_v2(Bucket=bucket)
        return sorted(obj["Key"] for obj in response.get("Contents", []))

    def read_object(self, bucket: str, key: str, compressed: bool = False) -> bytes:
        body = self.client.get_object(Bucket=bucket, Key=key)["Body"].read()
        return gzip.decompress(body) if compressed else body

    def read_object_lines(self, bucket: str, key: str, compressed: bool = False) -> List[str]:
        return self.read_object(bucket, key, compressed).decode("utf-8").splitlines()
