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
import typing


class LogPathIf(object):
    def __init__(
        self,
        if_elif_blocks: typing.List[typing.List[typing.Any]],
        else_block: typing.Optional[typing.List[typing.Any]] = None,
    ) -> None:
        assert if_elif_blocks
        self.__group_type = "log_if"
        self.__if_elif_blocks = if_elif_blocks
        self.__else_block = else_block

    @property
    def group_type(self):
        return self.__group_type

    @property
    def if_block(self) -> typing.List[typing.Any]:
        return self.__if_elif_blocks[0]

    @property
    def elif_blocks(self) -> typing.List[typing.List[typing.Any]]:
        return self.__if_elif_blocks[1:]

    @property
    def else_block(self) -> typing.Optional[typing.List[typing.Any]]:
        return self.__else_block
