#!/usr/bin/env python
#############################################################################
# Copyright (c) 2025 Axoflow
# Copyright (c) 2025 Attila Szakacs <attila.szakacs@axoflow.com>
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


class UnixDgramIO():
    def __init__(self, path: Path) -> None:
        self.__socket = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        self.__path = path

    def write_messages(self, messages: typing.List[str]) -> None:
        for message in messages:
            self.__socket.sendto(message.encode(), self.__path)

    def close_socket(self):
        self.__socket.close()
