#############################################################################
# Copyright (c) 2017 Balabit
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

find_path(LIBMAXMINDDB_INCLUDE_DIR NAMES maxminddb.h)
find_library(LIBMAXMINDDB_LIBRARY NAMES maxminddb maxminddb.a)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LIBMAXMINDDB DEFAULT_MSG
    LIBMAXMINDDB_LIBRARY
    LIBMAXMINDDB_INCLUDE_DIR
)

mark_as_advanced(
    LIBMAXMINDDB_LIBRARY
    LIBMAXMINDDB_INCLUDE_DIR
)
