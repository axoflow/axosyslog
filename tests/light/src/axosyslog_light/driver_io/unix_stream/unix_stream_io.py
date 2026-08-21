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
import socket
import typing
from pathlib import Path


class UnixStreamIO():
    def __init__(self, path: Path) -> None:
        self.__path = path
        self.__socket = None

    def __connect(self) -> None:
        if self.__socket is None:
            self.__socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.__socket.connect(str(self.__path))

    def write_raw(self, raw_content: str) -> None:
        self.__connect()
        self.__socket.sendall(raw_content.encode())

    def write_messages(self, messages: typing.List[str], terminator: str = "\n") -> None:
        self.write_raw("".join([message + terminator for message in messages]))

    def close_socket(self) -> None:
        if self.__socket is not None:
            self.__socket.close()
            self.__socket = None
