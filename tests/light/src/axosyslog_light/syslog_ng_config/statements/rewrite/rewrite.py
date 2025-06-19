#!/usr/bin/env python
#############################################################################
# Copyright (c) 2020 Balabit
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


class Rewrite(object):
    group_type = "rewrite"

    def __init__(self, driver_name, positional_parameters, **options):
        self.driver_name = driver_name
        self.options = options
        self.positional_parameters = positional_parameters


class SetTag(Rewrite):
    def __init__(self, tag, **options):
        super(SetTag, self).__init__("set_tag", [tag], **options)


class Set(Rewrite):
    def __init__(self, template, **options):
        super(Set, self).__init__("set", [template], **options)


class SetPri(Rewrite):
    def __init__(self, pri, **options):
        super(SetPri, self).__init__("set_pri", [pri], **options)


class CreditCardHash(Rewrite):
    def __init__(self, **options):
        super(CreditCardHash, self).__init__("credit-card-hash", [], **options)


class CreditCardMask(Rewrite):
    def __init__(self, **options):
        super(CreditCardMask, self).__init__("credit-card-mask", [], **options)
