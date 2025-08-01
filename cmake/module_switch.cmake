#############################################################################
# Copyright (c) 2019 Balabit
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

option(DISABLE_ALL_MODULES "disable all switchable modules by default" "OFF")

function (module_switch NAME HELP_STRING)
  if (DEFINED ${NAME})
    return()
  endif()

  if (DISABLE_ALL_MODULES)
    option(${NAME} ${HELP_STRING} "OFF")
    return ()
  endif()

  foreach (symbol ${ARGN})
    if (NOT ${symbol})
      option(${NAME} "${HELP_STRING}" "OFF")
      return()
    endif()
  endforeach()

  option(${NAME} "${HELP_STRING}" "ON")
endfunction ()
