#############################################################################
# Copyright (c) 2016 Balabit
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

include (CheckTypeSize)

set (CMAKE_EXTRA_INCLUDE_FILES sys/socket.h netinet/in.h)
set (CMAKE_REQUIRED_DEFINITIONS -D_GNU_SOURCE=1)
check_type_size ("struct sockaddr_in6" STRUCT_SOCKADDR_IN6)
if (HAVE_STRUCT_SOCKADDR_IN6)
    set(HAVE_IPV6 1 CACHE INTERNAL 1)
else()
    set(HAVE_IPV6 0 CACHE INTERNAL 0)
endif()
unset (CMAKE_EXTRA_INCLUDE_FILES)
unset (CMAKE_REQUIRED_DEFINITIONS)
