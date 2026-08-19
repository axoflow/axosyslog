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
import typing
from pathlib import Path

from axosyslog_light.common.file import File


class PipeIO():
    def __init__(self, path: Path, pad_size: int = 0) -> None:
        self.__pad_size = pad_size
        self.__file = File(path)

    def __open(self) -> None:
        if not self.__file.is_opened():
            # AxoSyslog creates the FIFO and holds it open O_RDWR, so this does not block.
            self.__file.wait_for_creation()
            self.__file.open("w")

    def __pad(self, record: str) -> str:
        if self.__pad_size == 0:
            return record
        if len(record) > self.__pad_size:
            raise ValueError("record of {} bytes does not fit into pad_size({})".format(len(record), self.__pad_size))
        return record + "\0" * (self.__pad_size - len(record))

    def write_raw(self, raw_content: str) -> None:
        self.__open()
        self.__file.write(raw_content)

    def write_messages(self, messages: typing.List[str], terminator: str = "\n") -> None:
        self.write_raw("".join([self.__pad(message + terminator) for message in messages]))

    def close_file(self) -> None:
        self.__file.close()
