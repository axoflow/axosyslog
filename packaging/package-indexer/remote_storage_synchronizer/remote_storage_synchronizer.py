#############################################################################
# Copyright (c) 2022 One Identity
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

from __future__ import annotations


import logging
from abc import ABC, abstractmethod
from enum import Enum, auto
from hashlib import md5
from pathlib import Path
from typing import Any, Dict, List, Optional


class WorkingDir:
    def __init__(self, root_dir: Path) -> None:
        self.__root_dir = root_dir
        self.__working_dir = root_dir

    @property
    def root_dir(self) -> Path:
        return self.__root_dir

    @property
    def working_dir(self) -> Path:
        return self.__working_dir

    def set_sub_dir(self, sub_dir: Path) -> None:
        self.__working_dir = Path(self.__root_dir, sub_dir)


class FileSyncState(Enum):
    IN_SYNC = auto()
    DIFFERENT = auto()
    NOT_IN_REMOTE = auto()
    NOT_IN_LOCAL = auto()


class RemoteStorageSynchronizer(ABC):
    def __init__(self, remote_root_dir: Path, local_root_dir: Path) -> None:
        self.remote_dir = WorkingDir(remote_root_dir)
        self.local_dir = WorkingDir(local_root_dir)

        self.__remote_files_cache: Optional[List[dict]] = None
        self.__logger = RemoteStorageSynchronizer.__create_logger()

    def set_sub_dir(self, sub_dir: Path) -> None:
        self.remote_dir.set_sub_dir(sub_dir)
        self.local_dir.set_sub_dir(sub_dir)

    @staticmethod
    @abstractmethod
    def get_config_keyword() -> str:
        pass

    @staticmethod
    @abstractmethod
    def from_config(cfg: dict) -> RemoteStorageSynchronizer:
        pass

    def sync_from_remote(self) -> None:
        self._log_info(
            "Syncing content from remote.",
            remote_workdir=str(self.remote_dir.working_dir),
            local_workdir=str(self.local_dir.working_dir),
        )
        for file in self._all_files:
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
        for file in self._all_files:
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

    @abstractmethod
    def _create_snapshot_of_remote(self) -> None:
        pass

    def create_snapshot_of_remote(self) -> None:
        self._log_info("Creating snapshot of remote")
        self._create_snapshot_of_remote()

    @property
    def _local_files(self) -> List[Path]:
        dirs_and_files = list(self.local_dir.working_dir.rglob("*"))
        return list(filter(lambda path: path.is_file(), dirs_and_files))

    @abstractmethod
    def _list_remote_files(self) -> List[Dict[str, Any]]:
        pass

    @property
    def _remote_files(self) -> List[Dict[str, Any]]:
        if self.__remote_files_cache is not None:
            return self.__remote_files_cache

        self.__remote_files_cache = self._list_remote_files()
        return self.__remote_files_cache

    def __invalidate_remote_files_cache(self) -> None:
        self.__remote_files_cache = None

    @property
    def _all_files(self) -> List[str]:
        files = set()
        for local_file in self._local_files:
            files.add(self.__get_relative_file_path_for_local_file(local_file))
        for remote_file in self._remote_files:
            files.add(self._get_relative_file_path_for_remote_file(remote_file))
        return sorted(files)

    @abstractmethod
    def _download_file(self, relative_file_path: str) -> None:
        pass

    def __download_file(self, relative_file_path: str) -> None:
        download_path = self._get_local_file_path_for_relative_file(relative_file_path)
        self._log_info("Downloading file.", remote_path=relative_file_path, local_path=str(download_path))
        self._download_file(relative_file_path)
        self._log_info("Successfully downloaded file.", remote_path=relative_file_path, local_path=str(download_path))

        md5sum = md5(download_path.read_bytes()).digest()
        md5sum_file_path = self.__get_remote_md5sum_file_path(relative_file_path)
        md5sum_file_path.parent.mkdir(exist_ok=True, parents=True)
        md5sum_file_path.write_bytes(md5sum)

    @abstractmethod
    def _upload_file(self, relative_file_path: str) -> None:
        pass

    def __upload_file(self, relative_file_path: str) -> None:
        local_path = self._get_local_file_path_for_relative_file(relative_file_path)

        self._log_info("Uploading file.", local_path=str(local_path), remote_path=relative_file_path)
        self._upload_file(relative_file_path)
        self._log_info("Successfully uploaded file.", remote_path=relative_file_path, local_path=str(local_path))

    def __delete_local_file(self, relative_file_path: str) -> None:
        local_file_path = Path(self.local_dir.root_dir, relative_file_path).resolve()
        self._log_info("Deleting local file.", local_path=str(local_file_path))
        local_file_path.unlink()
        self._log_info("Successfully deleted local file.", local_path=str(local_file_path))

    @abstractmethod
    def _delete_remote_file(self, relative_file_path: str) -> None:
        pass

    def __delete_remote_file(self, relative_file_path: str) -> None:
        self._log_info("Deleting remote file.", remote_path=relative_file_path)
        self._delete_remote_file(relative_file_path)
        self._log_info("Successfully deleted remote file.", remote_path=relative_file_path)

    def __get_relative_file_path_for_local_file(self, file: Path) -> str:
        return str(file.relative_to(self.local_dir.root_dir))

    @abstractmethod
    def _get_relative_file_path_for_remote_file(self, file: Dict[str, Any]) -> str:
        pass

    def _get_local_file_path_for_relative_file(self, relative_file_path: str) -> Path:
        return Path(self.local_dir.root_dir, relative_file_path).resolve()

    def __get_md5_of_local_file(self, relative_file_path: str) -> bytes:
        file = Path(self.local_dir.root_dir, relative_file_path)
        return md5(file.read_bytes()).digest()

    def __get_remote_md5sum_file_path(self, relative_file_path: str) -> Path:
        path = Path(self.local_dir.root_dir, "package-indexer-md5sums", relative_file_path).resolve()
        return Path(path.parent, path.name + ".package-indexer-md5sum")

    def __get_md5_of_remote_file(self, relative_file_path: str) -> bytes:
        md5sum_file_path = self.__get_remote_md5sum_file_path(relative_file_path)
        return md5sum_file_path.read_bytes()

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

    @staticmethod
    def __create_logger() -> logging.Logger:
        logger = logging.getLogger("RemoteStorageSynchronizer")
        logger.setLevel(logging.DEBUG)
        return logger

    def _prepare_log(self, message: str, **kwargs: str) -> str:
        if len(kwargs) > 0:
            message += "\t{}".format(kwargs)
        return message

    def _log_error(self, message: str, **kwargs: str) -> None:
        log = self._prepare_log(message, **kwargs)
        self.__logger.error(log)

    def _log_info(self, message: str, **kwargs: str) -> None:
        log = self._prepare_log(message, **kwargs)
        self.__logger.info(log)

    def _log_debug(self, message: str, **kwargs: str) -> None:
        log = self._prepare_log(message, **kwargs)
        self.__logger.debug(log)
