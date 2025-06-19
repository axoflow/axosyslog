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

include(LibFindMacros)

libfind_pkg_detect(Riemann riemann-client FIND_PATH riemann/event.h FIND_LIBRARY riemann-client)
if (NOT Riemann_LIBRARY)
  libfind_pkg_detect(Riemann riemann-client FIND_PATH riemann/event.h FIND_LIBRARY riemann-client-openssl)
endif()
if (NOT Riemann_LIBRARY)
  libfind_pkg_detect(Riemann riemann-client FIND_PATH riemann/event.h FIND_LIBRARY riemann-client-no-tls)
endif()
if (Riemann_LIBRARY)
      message(STATUS "Found Riemann client: ${Riemann_LIBRARY}")
endif()

set(Riemann_PROCESS_INCLUDES Riemann_INCLUDE_DIR)
set(Riemann_PROCESS_LIBS Riemann_LIBRARY)

# silence warnings
libfind_process(Riemann QUIET)

set(CMAKE_EXTRA_INCLUDE_FILES "event.h")
set(CMAKE_REQUIRED_INCLUDES "${Riemann_INCLUDE_DIR}/riemann/")
set(CMAKE_REQUIRED_LIBRARIES "${Riemann_LIBRARIES}")

check_type_size(RIEMANN_EVENT_FIELD_TIME_MICROS RIEMANN_MICROSECONDS)
