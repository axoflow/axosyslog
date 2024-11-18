#############################################################################
# Copyright (c) 2024 Axoflow
# Copyright (c) 2024 Attila Szakacs <attila.szakacs@axoflow.com>
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

import shutil
import re
from pathlib import Path
from tempfile import TemporaryDirectory, TemporaryFile, NamedTemporaryFile
from typing import List, Optional

from cdn import CDN
from remote_storage_synchronizer import RemoteStorageSynchronizer

from indexer import Indexer

from . import utils

CURRENT_DIR = Path(__file__).parent.resolve()


class RpmIndexer(Indexer):
    def __init__(
        self,
        incoming_remote_storage_synchronizer: RemoteStorageSynchronizer,
        indexed_remote_storage_synchronizer: RemoteStorageSynchronizer,
        incoming_sub_dir: Path,
        dist_dir: Path,
        cdn: CDN,
        gpg_key_path: Path,
        gpg_key_passphrase: Optional[str],
    ) -> None:
        self.__gpg_key_path = gpg_key_path
        self.__gpg_key_passphrase = gpg_key_passphrase
        super().__init__(
            incoming_remote_storage_synchronizer=incoming_remote_storage_synchronizer,
            indexed_remote_storage_synchronizer=indexed_remote_storage_synchronizer,
            incoming_sub_dir=incoming_sub_dir,
            indexed_sub_dir=Path("rpm", dist_dir),
            cdn=cdn,
        )

    def __move_files_from_incoming_to_indexed(self, incoming_dir: Path, indexed_dir: Path) -> None:
        for file in filter(lambda path: path.is_file(), incoming_dir.rglob("*")):
            relative_path = file.relative_to(incoming_dir)
            platform = relative_path.parent
            file_name = relative_path.name

            new_path = Path(indexed_dir, platform, "x86_64", file_name)

            self._log_info("Moving file.", src_path=str(file), dst_path=str(new_path))

            new_path.parent.mkdir(parents=True, exist_ok=True)
            utils.move_file_without_overwrite(file, new_path)

    def _prepare_indexed_dir(self, incoming_dir: Path, indexed_dir: Path) -> None:
        self.__move_files_from_incoming_to_indexed(incoming_dir, indexed_dir)

    def __get_gpg_key_email(self, gnupghome_dir: Path) -> str:
        with TemporaryFile(mode="w+", encoding="utf-8") as f:
            utils.execute_command(
                ["gpg", "--list-secret-keys", "--with-colons"],
                stdout=f,
                env={"GNUPGHOME": str(gnupghome_dir)},
            )
            f.seek(0)

            for line in f.readlines():
                if line.startswith("uid"):
                    return line.split(":")[9]

        raise Exception("Failed to get GPG key email.")

    def __rpm_addsign(self, gnupghome_dir: Path, indexed_dir: Path) -> None:
        email = self.__get_gpg_key_email(gnupghome_dir)

        sign_cmd = r"%{__gpg} gpg --no-tty --no-options --batch --no-armor -u \"%{_gpg_name}\" -sbo %{__signature_filename} %{__plaintext_filename}'"

        rpmmacros_path = Path(gnupghome_dir, ".rpmmacros").absolute()
        rpmmacros_path.write_text(
            "%_signature gpg\n"
            f"%_gpg_name {email}\n"
            f"%_gpg_path {str(gnupghome_dir)}\n"
            f"%__gpg {shutil.which('gpg')}\n"
            f"%__gpg_sign_cmd {sign_cmd}\n"
        )

        env = {"GNUPGHOME": str(gnupghome_dir), "GPG_TTY": ""}

        for rpm_file in indexed_dir.rglob("*.rpm"):
            command = ["rpm", "--macros", str(rpmmacros_path), "--addsign", str(rpm_file.absolute())]

            self._log_info("Signing rpm.", rpm_file=str(rpm_file))
            utils.execute_command(command, env=env)

    def __create_repomd_xml_asc_file(self, gnupghome_dir: Path, repo_path: Path) -> None:
        repomd_xml_asc_path = Path(repo_path, "repodata", "repomd.xml.asc")
        if repomd_xml_asc_path.exists():
            self._log_info("Removing old `repomd.xml.asc` file.", repomd_xml_asc_path=str(repomd_xml_asc_path))
            repomd_xml_asc_path.unlink()

        command = [
            "gpg",
            "--armor",
            "--detach-sign",
            "--sign",
            str(Path(repo_path, "repodata", "repomd.xml")),
        ]
        command = self.__add_gpg_security_params(command)

        self._log_info("Creating `repomd.xml.asc` file.", repomd_xml_asc_path=str(repomd_xml_asc_path))
        utils.execute_command(command, env={"GNUPGHOME": str(gnupghome_dir)})

    def __createrepo(self, gnupghome_dir: Path, indexed_dir: Path) -> None:
        for d in indexed_dir.glob("*"):
            repo_path = Path(indexed_dir, d, "x86_64")

            self._log_info("Creating repo.", repo_path=str(repo_path))
            utils.execute_command(["createrepo_c", "--update", str(repo_path)])

            self.__create_repomd_xml_asc_file(gnupghome_dir, repo_path)

    def _index_pkgs(self, indexed_dir: Path) -> None:
        gnupghome = TemporaryDirectory(dir=CURRENT_DIR)
        gnupghome_dir = Path(gnupghome.name)

        try:
            self.__start_gpg_agent_if_needed(gnupghome_dir)
            self.__add_gpg_key_to_chain(gnupghome_dir)
            self.__cache_gpg_key_passphase_if_needed(gnupghome_dir)
            self.__rpm_addsign(gnupghome_dir, indexed_dir)
            self.__createrepo(gnupghome_dir, indexed_dir)
        finally:
            self.__stop_gpg_agent_if_needed(gnupghome)
            self._log_info("Cleaning up `GNUPGHOME` directory.", gnupghome=gnupghome.name)
            gnupghome.cleanup()

    @staticmethod
    def __add_gpg_security_params(command: list) -> list:
        assert command[0] == "gpg"

        return [
            "gpg",
            "--no-tty",
            "--no-options",
        ] + command[1:]

    def __add_gpg_passphrase_params_if_needed(self, command: list) -> list:
        if self.__gpg_key_passphrase is None:
            return command

        assert command[0] == "gpg"

        return [
            "gpg",
            "--batch",
            "--pinentry-mode",
            "loopback",
            "--passphrase-fd",
            "0",
        ] + command[1:]

    def __start_gpg_agent_if_needed(self, gnupghome_dir: Path) -> None:
        if self.__gpg_key_passphrase is None:
            return

        command = ["gpg-agent", "--daemon", "--homedir", str(gnupghome_dir)]
        env = {"GNUPGHOME": str(gnupghome_dir)}

        self._log_info("Starting GPG agent.", gnupghome_dir=str(gnupghome_dir))
        utils.execute_command(command, env=env)

    def __add_gpg_key_to_chain(self, gnupghome_dir: Path) -> None:
        command = ["gpg", "--import", str(self.__gpg_key_path)]
        command = self.__add_gpg_security_params(command)
        command = self.__add_gpg_passphrase_params_if_needed(command)
        env = {"GNUPGHOME": str(gnupghome_dir)}

        self._log_info("Adding GPG key to chain.", gpg_key_path=str(self.__gpg_key_path))
        utils.execute_command(command, env=env, input=self.__gpg_key_passphrase)

    def __cache_gpg_key_passphase_if_needed(self, gnupghome_dir: Path) -> None:
        if self.__gpg_key_passphrase is None:
            return

        with NamedTemporaryFile(mode="w+", encoding="utf-8") as f:
            command = [
                "gpg",
                "--armor",
                "--detach-sign",
                "--sign",
                f.name,
            ]
            command = self.__add_gpg_security_params(command)
            command = self.__add_gpg_passphrase_params_if_needed(command)
            env = {"GNUPGHOME": str(gnupghome_dir)}

            self._log_info("Caching GPG key passphrase.", gnupghome_dir=str(gnupghome_dir))
            utils.execute_command(command, env=env, input=self.__gpg_key_passphrase)

    def __stop_gpg_agent_if_needed(self, gnupghome: TemporaryDirectory) -> None:
        if self.__gpg_key_passphrase is None:
            return

        command = ["gpgconf", "--kill", "gpg-agent"]
        env = {"GNUPGHOME": str(gnupghome.name)}

        self._log_info("Stopping GPG agent.", gnupghome=gnupghome.name)
        utils.execute_command(command, env=env)


class StableRpmIndexer(RpmIndexer):
    def __init__(
        self,
        incoming_remote_storage_synchronizer: RemoteStorageSynchronizer,
        indexed_remote_storage_synchronizer: RemoteStorageSynchronizer,
        run_id: str,
        cdn: CDN,
        gpg_key_path: Path,
        gpg_key_passphrase: Optional[str],
    ) -> None:
        super().__init__(
            incoming_remote_storage_synchronizer=incoming_remote_storage_synchronizer,
            indexed_remote_storage_synchronizer=indexed_remote_storage_synchronizer,
            incoming_sub_dir=Path("stable", run_id, "rpm"),
            dist_dir=Path("stable"),
            cdn=cdn,
            gpg_key_path=gpg_key_path,
            gpg_key_passphrase=gpg_key_passphrase,
        )


class NightlyRpmIndexer(RpmIndexer):
    PKGS_TO_KEEP = 10

    def __init__(
        self,
        incoming_remote_storage_synchronizer: RemoteStorageSynchronizer,
        indexed_remote_storage_synchronizer: RemoteStorageSynchronizer,
        cdn: CDN,
        run_id: str,
        gpg_key_path: Path,
        gpg_key_passphrase: Optional[str],
    ) -> None:
        super().__init__(
            incoming_remote_storage_synchronizer=incoming_remote_storage_synchronizer,
            indexed_remote_storage_synchronizer=indexed_remote_storage_synchronizer,
            incoming_sub_dir=Path("nightly", run_id, "rpm"),
            dist_dir=Path("nightly"),
            cdn=cdn,
            gpg_key_path=gpg_key_path,
            gpg_key_passphrase=gpg_key_passphrase,
        )

    def __get_pkg_timestamps_in_dir(self, dir: Path) -> List[str]:
        timestamp_regexp = re.compile(r"\+([^.]+).")
        pkg_timestamps: List[str] = []

        for deb_file in dir.rglob("axosyslog-devel*.rpm"):
            pkg_timestamp = timestamp_regexp.findall(deb_file.name)[0]
            pkg_timestamps.append(pkg_timestamp)
        pkg_timestamps.sort()

        return pkg_timestamps

    def __remove_pkgs_with_timestamp(self, dir: Path, timestamps_to_remove: List[str]) -> None:
        for timestamp in timestamps_to_remove:
            for deb_file in dir.rglob("*{}*.rpm".format(timestamp)):
                self._log_info("Removing old nightly package.", path=str(deb_file.resolve()))
                deb_file.unlink()

    def __remove_old_pkgs(self, indexed_dir: Path) -> None:
        platform_dirs = list(filter(lambda path: path.is_dir(), indexed_dir.glob("*")))

        for platform_dir in platform_dirs:
            pkg_timestamps = self.__get_pkg_timestamps_in_dir(platform_dir)

            if len(pkg_timestamps) <= NightlyRpmIndexer.PKGS_TO_KEEP:
                continue

            timestamps_to_remove = pkg_timestamps[: -NightlyRpmIndexer.PKGS_TO_KEEP]
            self.__remove_pkgs_with_timestamp(platform_dir, timestamps_to_remove)

    def _prepare_indexed_dir(self, incoming_dir: Path, indexed_dir: Path) -> None:
        super()._prepare_indexed_dir(incoming_dir, indexed_dir)
        self.__remove_old_pkgs(indexed_dir)
