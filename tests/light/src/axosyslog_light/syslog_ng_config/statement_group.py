#!/usr/bin/env python
#############################################################################
# Copyright (c) 2015-2018 Balabit
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
import functools

from axosyslog_light.common.operations import cast_to_list
from axosyslog_light.common.random_id import get_unique_id


class StatementGroup(list):
    def __init__(self, statements):
        super(StatementGroup, self).__init__(cast_to_list(statements))
        self.__group_type = self.__calculate_group_type(cast_to_list(statements))
        self.__group_id = "{}_{}".format(self.__group_type, get_unique_id(length=30))

    @staticmethod
    def __calculate_group_type(statements):
        def check_consistency(stmt1, stmt2):
            type1 = stmt1.group_type
            type2 = stmt2.group_type
            if type1 != type2:
                raise TypeError("Conflict in statement types: {} and {}".format(type1, type2))
            return stmt2

        return functools.reduce(check_consistency, statements).group_type

    @property
    def group_type(self):
        return self.__group_type

    @property
    def group_id(self):
        return self.__group_id
