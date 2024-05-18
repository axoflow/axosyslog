#############################################################################
# Copyright (c) 2022 One Identity
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
from typing import Any, Dict, List

from azure.storage.blob import BlobClient, ContainerClient

from .remote_storage_synchronizer import RemoteStorageSynchronizer

DEFAULT_ROOT_DIR = Path("/tmp/azure_container_synchronizer")


class AzureContainerSynchronizer(RemoteStorageSynchronizer):
    """
    A `RemoteStorageSynchronizer` implementation that can connect to an Azure Blob Container Storage instance.

    Example config:

    ```yaml
    vendor: "azure"
    incoming:
      all:
        storage-name: "incoming"
        connection-string: "secret1"
    indexed:
      stable:
        storage-name: "indexed"
        connection-string: "secret2"
      nightly:
        storage-name: "indexed"
        connection-string: "secret3"
    ```
    """

    def __init__(self, connection_string: str, storage_name: str) -> None:
        self.__client = ContainerClient.from_connection_string(conn_str=connection_string, container_name=storage_name)
        super().__init__(
            remote_root_dir=Path(""),
            local_root_dir=Path(DEFAULT_ROOT_DIR, storage_name),
        )

    @staticmethod
    def get_config_keyword() -> str:
        return "azure"

    @staticmethod
    def from_config(cfg: dict) -> RemoteStorageSynchronizer:
        return AzureContainerSynchronizer(
            connection_string=cfg["connection-string"],
            storage_name=cfg["storage-name"],
        )

    def _list_remote_files(self) -> List[Dict[str, Any]]:
        file_name_prefix = "{}/".format(self.remote_dir.working_dir)
        return [dict(blob) for blob in self.__client.list_blobs(name_starts_with=file_name_prefix)]

    def _download_file(self, relative_file_path: str) -> None:
        download_path = Path(self.local_dir.root_dir, relative_file_path).resolve()
        download_path.parent.mkdir(parents=True, exist_ok=True)
        with download_path.open("wb") as downloaded_blob:
            blob_data = self.__client.download_blob(relative_file_path)
            blob_data.readinto(downloaded_blob)

    def _upload_file(self, relative_file_path: str) -> None:
        local_path = Path(self.local_dir.root_dir, relative_file_path)
        with local_path.open("rb") as local_file_data:
            self.__client.upload_blob(relative_file_path, local_file_data, overwrite=True)

    def _delete_remote_file(self, relative_file_path: str) -> None:
        self.__client.delete_blob(relative_file_path, delete_snapshots="include")

    def _create_snapshot_of_remote(self) -> None:
        for file in self._remote_files:
            blob_client: BlobClient = self.__client.get_blob_client(file["name"])
            snapshot_properties = blob_client.create_snapshot()
            self._log_debug(
                "Successfully created snapshot of remote file.",
                remote_path=self._get_relative_file_path_for_remote_file(file),
                snapshot_properties=str(snapshot_properties),
            )

    def _get_md5_of_remote_file(self, relative_file_path: str) -> bytes:
        for file in self._remote_files:
            if file["name"] == relative_file_path:
                return file["content_settings"]["content_md5"]
        raise FileNotFoundError

    def _get_relative_file_path_for_remote_file(self, file: Dict[str, Any]) -> str:
        return file["name"]

    def _prepare_log(self, message: str, **kwargs: str) -> str:
        log = "[{} :: {}]\t{}".format(self.__client.container_name, str(self.remote_dir.working_dir), message)
        return super()._prepare_log(log, **kwargs)
