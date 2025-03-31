#!/usr/bin/env python
#############################################################################
# Copyright (c) 2025 Axoflow
# Copyright (c) 2025 Attila Szakacs <attila.szakacs@axoflow.com>
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

import pickle
import typing
from pathlib import Path

from filelock import FileLock


class SessionData:
    SINGLETON: typing.Optional[SessionData] = None
    SESSION_FILE = Path("axosyslog-light.session.pickle").resolve().absolute()
    SESSION_LOCK_FILE = Path("axosyslog-light.lock").resolve().absolute()

    def __init__(self) -> None:
        if SessionData.SINGLETON is not None:
            raise ValueError("SessionData is a singleton, cannot create more than one instance")
        SessionData.SINGLETON = self

        self.__data: typing.Dict[str, typing.Any] = dict()
        self.__lock = FileLock(SessionData.SESSION_LOCK_FILE)

        with self.__lock:
            self.__sync_from_file()

    @staticmethod
    def get_singleton() -> SessionData:
        if SessionData.SINGLETON is None:
            SessionData.SINGLETON = SessionData()
        return SessionData.SINGLETON

    @staticmethod
    def cleanup() -> None:
        if get_session_data().__lock.is_locked:
            raise Exception("SessionData.cleanup() must be used without the 'with' statement")

        SessionData.SESSION_FILE.unlink(missing_ok=True)
        SessionData.SESSION_LOCK_FILE.unlink(missing_ok=True)

    def __sync_from_file(self) -> None:
        if not SessionData.SESSION_FILE.exists():
            return

        with SessionData.SESSION_FILE.open("rb") as f:
            self.__data.update(pickle.load(f))

    def __sync_to_file(self) -> None:
        with SessionData.SESSION_FILE.open("wb") as f:
            pickle.dump(self.__data, f)

    def __enter__(self) -> SessionData:
        self.__lock.acquire()
        self.__sync_from_file()
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> None:
        self.__sync_to_file()
        self.__lock.release()

    def __ensure_with(self) -> None:
        if not self.__lock.is_locked:
            raise ValueError("SessionData must be used with the 'with' statement")

    def __getitem__(self, key: str) -> typing.Any:
        self.__ensure_with()
        return self.__data[key]

    def __setitem__(self, key: str, value: typing.Any) -> None:
        self.__ensure_with()
        self.__data[key] = value

    def get(self, key: str, default: typing.Any = None) -> typing.Any:
        self.__ensure_with()
        return self.__data.get(key, default)


def get_session_data() -> SessionData:
    return SessionData.get_singleton()
