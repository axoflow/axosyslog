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

from hashlib import md5
from pathlib import Path
from typing import Any, Dict, List, Optional, Set

from boto3 import Session
from botocore.exceptions import ClientError, EndpointConnectionError

from .remote_storage_synchronizer import FileSyncState, RemoteStorageSynchronizer

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
        self.__remote_files_cache: Optional[List[dict]] = None
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

    @property
    def local_files(self) -> List[Path]:
        dirs_and_files = list(self.local_dir.working_dir.rglob("*"))
        return list(
            filter(lambda path: path.is_file() and not path.name.endswith(".package-indexer-md5sum"), dirs_and_files)
        )

    def __list_existing_objects(self) -> List[Dict[str, Any]]:
        objects: List[Dict[str, Any]] = []
        pagination_options: Dict[str, str] = {}

        while True:
            try:
                response: Dict[str, Any] = self.__client.list_objects(
                    Bucket=self.__bucket,
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

    @property
    def remote_files(self) -> List[Dict[str, Any]]:
        if self.__remote_files_cache is not None:
            return self.__remote_files_cache

        self.__remote_files_cache = self.__list_existing_objects()
        return self.__remote_files_cache

    def __get_remote_md5sum_file_path(self, relative_file_path: str) -> Path:
        path = Path(self.local_dir.root_dir, "package-indexer-md5sums", relative_file_path).resolve()
        return Path(path.parent, path.name + ".package-indexer-md5sum")

    def __download_file(self, relative_file_path: str) -> None:
        download_path = Path(self.local_dir.root_dir, relative_file_path).resolve()

        self._log_info(
            "Downloading file.",
            remote_path=relative_file_path,
            local_path=str(download_path),
        )

        download_path.parent.mkdir(parents=True, exist_ok=True)
        with download_path.open("wb") as downloaded_object:
            self.__client.download_fileobj(self.__bucket, relative_file_path, downloaded_object)

        md5sum = md5(download_path.read_bytes()).digest()
        md5sum_file_path = self.__get_remote_md5sum_file_path(relative_file_path)
        md5sum_file_path.parent.mkdir(exist_ok=True, parents=True)
        md5sum_file_path.write_bytes(md5sum)

    def __upload_file(self, relative_file_path: str) -> None:
        local_path = Path(self.local_dir.root_dir, relative_file_path)

        self._log_info(
            "Uploading file.",
            local_path=str(local_path),
            remote_path=relative_file_path,
        )

        with local_path.open("rb") as local_file_data:
            self.__client.upload_fileobj(local_file_data, self.__bucket, relative_file_path)

    def __delete_local_file(self, relative_file_path: str) -> None:
        local_file_path = Path(self.local_dir.root_dir, relative_file_path).resolve()
        self._log_info("Deleting local file.", local_path=str(local_file_path))
        local_file_path.unlink()

    def __delete_remote_file(self, relative_file_path: str) -> None:
        self._log_info("Deleting remote file.", remote_path=relative_file_path)
        self.__client.delete_object(Bucket=self.__bucket, Key=relative_file_path)

    def sync_from_remote(self) -> None:
        self._log_info(
            "Syncing content from remote.",
            remote_workdir=str(self.remote_dir.working_dir),
            local_workdir=str(self.local_dir.working_dir),
        )
        for file in self.__all_files:

            sync_state = self.__get_file_sync_state(file)
            if sync_state == FileSyncState.IN_SYNC:
                continue
            if sync_state == FileSyncState.DIFFERENT or sync_state == FileSyncState.NOT_IN_LOCAL:
                self.__download_file(file)
                continue
            if sync_state == FileSyncState.NOT_IN_REMOTE:
                self.__delete_local_file(file)
                continue
            raise NotImplementedError("Unexpected FileSyncState: {}".format(sync_state))
        self._log_info(
            "Successfully synced remote content.",
            remote_workdir=str(self.remote_dir.working_dir),
            local_workdir=str(self.local_dir.working_dir),
        )

    def sync_to_remote(self) -> None:
        self._log_info(
            "Syncing content to remote.",
            local_workdir=str(self.local_dir.working_dir),
            remote_workdir=str(self.remote_dir.working_dir),
        )
        for file in self.__all_files:
            sync_state = self.__get_file_sync_state(file)
            if sync_state == FileSyncState.IN_SYNC:
                continue
            if sync_state == FileSyncState.DIFFERENT or sync_state == FileSyncState.NOT_IN_REMOTE:
                self.__upload_file(file)
                continue
            if sync_state == FileSyncState.NOT_IN_LOCAL:
                self.__delete_remote_file(file)
                continue
            raise NotImplementedError("Unexpected FileSyncState: {}".format(sync_state))
        self._log_info(
            "Successfully synced local content.",
            remote_workdir=str(self.remote_dir.working_dir),
            local_workdir=str(self.local_dir.working_dir),
        )
        self.__invalidate_remote_files_cache()

    def create_snapshot_of_remote(self) -> None:
        self._log_info("Cannot create snapshot, not implemented, skipping...")

    def __get_md5_of_remote_file(self, relative_file_path: str) -> bytes:
        md5sum_file_path = self.__get_remote_md5sum_file_path(relative_file_path)
        return md5sum_file_path.read_bytes()

    def __get_md5_of_local_file(self, relative_file_path: str) -> bytes:
        file = Path(self.local_dir.root_dir, relative_file_path)
        return md5(file.read_bytes()).digest()

    def __get_file_sync_state(self, relative_file_path: str) -> FileSyncState:
        try:
            local_md5 = self.__get_md5_of_local_file(relative_file_path)
        except FileNotFoundError:
            self._log_debug(
                "Remote file is not available locally.",
                remote_path=str(Path(self.remote_dir.root_dir, relative_file_path)),
                unavailable_local_path=str(Path(self.local_dir.root_dir, relative_file_path)),
            )
            return FileSyncState.NOT_IN_LOCAL

        try:
            remote_md5 = self.__get_md5_of_remote_file(relative_file_path)
        except FileNotFoundError:
            self._log_debug(
                "Local file is not available remotely.",
                local_path=str(Path(self.local_dir.root_dir, relative_file_path)),
                unavailable_remote_path=str(Path(self.remote_dir.root_dir, relative_file_path)),
            )
            return FileSyncState.NOT_IN_REMOTE

        if remote_md5 != local_md5:
            self._log_debug(
                "File differs locally and remotely.",
                remote_path=str(Path(self.remote_dir.root_dir, relative_file_path)),
                local_path=str(Path(self.local_dir.root_dir, relative_file_path)),
                remote_md5sum=remote_md5.hex(),
                local_md5sum=local_md5.hex(),
            )
            return FileSyncState.DIFFERENT

        self._log_debug(
            "File is in sync.",
            remote_path=str(Path(self.remote_dir.root_dir, relative_file_path)),
            local_path=str(Path(self.local_dir.root_dir, relative_file_path)),
            md5sum=remote_md5.hex(),
        )
        return FileSyncState.IN_SYNC

    def __get_relative_file_path_for_local_file(self, file: Path) -> str:
        return str(file.relative_to(self.local_dir.root_dir))

    def __get_relative_file_path_for_remote_file(self, file: dict) -> str:
        return file["Key"]

    @property
    def __all_files(self) -> List[str]:
        files = set()
        for local_file in self.local_files:
            files.add(self.__get_relative_file_path_for_local_file(local_file))
        for remote_file in self.remote_files:
            files.add(self.__get_relative_file_path_for_remote_file(remote_file))
        return sorted(files)

    def __invalidate_remote_files_cache(self) -> None:
        self.__remote_files_cache = None

    def _prepare_log(self, message: str, **kwargs: str) -> str:
        log = "[{} :: {}]\t{}".format(self.__bucket, str(self.remote_dir.working_dir), message)
        return super()._prepare_log(log, **kwargs)
