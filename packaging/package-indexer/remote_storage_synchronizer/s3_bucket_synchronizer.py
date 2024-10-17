#############################################################################
# Copyright (c) 2024 Attila Szakacs
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

from pathlib import Path
from typing import Any, Dict, List

from boto3 import Session
from botocore.exceptions import ClientError, EndpointConnectionError

from .remote_storage_synchronizer import RemoteStorageSynchronizer

DEFAULT_ROOT_DIR = Path("/tmp/s3_bucket_synchronizer")


class S3BucketSynchronizer(RemoteStorageSynchronizer):
    """
    A `RemoteStorageSynchronizer` implementation that can connect to an S3 Bucket instance.

    Example config:

    ```yaml
    vendor: "s3"
    incoming:
      all:
        endpoint: "url1"
        bucket: "bucket1"
        access-key: "accesskey1"
        secret-key: "secretkey1"
    indexed:
      stable:
        endpoint: "url2"
        bucket: "bucket2"
        access-key: "accesskey2"
        secret-key: "secretkey2"
      nightly:
        endpoint: "url3"
        bucket: "bucket3"
        access-key: "accesskey3"
        secret-key: "secretkey3"
    ```
    """

    def __init__(self, access_key: str, secret_key: str, endpoint: str, bucket: str) -> None:
        self.__session = Session(
            aws_access_key_id=access_key,
            aws_secret_access_key=secret_key,
        )
        self.__client = self.__session.client(
            service_name="s3",
            endpoint_url=endpoint,
        )
        self.__bucket = bucket
        super().__init__(
            remote_root_dir=Path(""),
            local_root_dir=Path(DEFAULT_ROOT_DIR, bucket),
        )

    @staticmethod
    def get_config_keyword() -> str:
        return "s3"

    @staticmethod
    def from_config(cfg: dict) -> RemoteStorageSynchronizer:
        return S3BucketSynchronizer(
            access_key=cfg["access-key"],
            secret_key=cfg["secret-key"],
            endpoint=cfg["endpoint"],
            bucket=cfg["bucket"],
        )

    def _list_remote_files(self) -> List[Dict[str, Any]]:
        objects: List[Dict[str, Any]] = []
        pagination_options: Dict[str, str] = {}

        file_name_prefix = "{}/".format(self.remote_dir.working_dir)

        while True:
            try:
                response: Dict[str, Any] = self.__client.list_objects(
                    Bucket=self.__bucket,
                    Prefix=file_name_prefix,
                    **pagination_options,
                )
            except (ClientError, EndpointConnectionError) as e:
                self._log_error(f"Failed to list objects of bucket: {self.__bucket} => {e}")
                return []

            try:
                for obj in response.get("Contents", []):
                    objects.append(obj)
                if not response["IsTruncated"]:
                    break
                pagination_options = {"Marker": response["Contents"][-1]["Key"]}
            except KeyError:
                self._log_error(
                    f"Failed to list objects of bucket: {self.__bucket}/ => Unexpected response: {response}"
                )
                return []

        return objects

    def _download_file(self, relative_file_path: str) -> None:
        download_path = self._get_local_file_path_for_relative_file(relative_file_path)
        download_path.parent.mkdir(parents=True, exist_ok=True)
        with download_path.open("wb") as downloaded_object:
            self.__client.download_fileobj(self.__bucket, relative_file_path, downloaded_object)

    def _upload_file(self, relative_file_path: str) -> None:
        local_path = self._get_local_file_path_for_relative_file(relative_file_path)
        with local_path.open("rb") as local_file_data:
            self.__client.upload_fileobj(local_file_data, self.__bucket, relative_file_path)

    def _delete_remote_file(self, relative_file_path: str) -> None:
        self.__client.delete_object(Bucket=self.__bucket, Key=relative_file_path)

    def _create_snapshot_of_remote(self) -> None:
        self._log_info("Cannot create snapshot, not implemented, skipping...")

    def _get_relative_file_path_for_remote_file(self, file: Dict[str, Any]) -> str:
        return file["Key"]

    def _prepare_log(self, message: str, **kwargs: str) -> str:
        log = "[{} :: {}]\t{}".format(self.__bucket, str(self.remote_dir.working_dir), message)
        return super()._prepare_log(log, **kwargs)
