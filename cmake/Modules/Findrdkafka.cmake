#############################################################################
# Copyright (c) 2018 Balabit
# Copyright (c) 2018 Kokan
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

pkg_search_module(RDKAFKA_PKGCONF  rdkafka)

libfind_pkg_detect(RDKAFKA rdkafka FIND_PATH librdkafka/rdkafka.h FIND_LIBRARY rdkafka)
set(RDKAFKA_PROCESS_INCLUDES RDKAFKA_INCLUDE_DIR)
set(RDKAFKA_PROCESS_LIBS RDKAFKA_LIBRARY)
libfind_process(RDKAFKA)

add_library(rdkafka INTERFACE)

if (NOT RDKAFKA_FOUND)
 return()
endif()

if (${rdkafka_FIND_VERSION} VERSION_GREATER ${RDKAFKA_PKGCONF_VERSION})
 set(RDKAFKA_FOUND FALSE)
 return()
endif()

target_include_directories(rdkafka INTERFACE ${RDKAFKA_PROCESS_INCLUDES})
target_link_libraries(rdkafka INTERFACE ${RDKAFKA_LIBRARY})

