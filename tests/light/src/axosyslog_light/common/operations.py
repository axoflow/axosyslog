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
import os


def cast_to_list(items):
    if isinstance(items, list):
        return items
    return [items]


def ignore_asan_memleaks(func):
    @functools.wraps(func)
    def decorated_func(*args, **kwargs):
        original_asan_options = os.environ.get("ASAN_OPTIONS", "")
        os.environ["ASAN_OPTIONS"] = f"{original_asan_options}:detect_leaks=0"
        try:
            return func(*args, **kwargs)
        finally:
            os.environ["ASAN_OPTIONS"] = original_asan_options
    return decorated_func
