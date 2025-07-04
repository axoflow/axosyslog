#############################################################################
# Copyright (c) 2018 Balabit
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

include(LibFindMacros)
include(FindPackageHandleStandardArgs)

find_package(PkgConfig)

pkg_check_modules(PC_LIBCAP libcap QUIET)
find_path(LIBCAP_INCLUDE_DIR NAMES sys/capability.h HINTS ${PC_LIBCAP_INCLUDE_DIRS})
find_library(LIBCAP_LIBRARY  NAMES cap              HINTS ${PC_LIBCAP_LIBRARY_DIRS})

add_library(libcap INTERFACE)

if (NOT PC_LIBCAP_FOUND)
 return()
endif()

target_include_directories(libcap INTERFACE ${LIBCAP_INCLUDE_DIR})
target_link_libraries(libcap INTERFACE ${LIBCAP_LIBRARY})

