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

# This function is syslog-ng specific

function(generate_y_from_ym FileWoExt)
    if (${ARGC} EQUAL 1)
        add_custom_command (OUTPUT ${PROJECT_BINARY_DIR}/${FileWoExt}.y
            COMMAND ${PYTHON_EXECUTABLE} ${PROJECT_SOURCE_DIR}/lib/merge-grammar.py ${PROJECT_SOURCE_DIR}/${FileWoExt}.ym > ${PROJECT_BINARY_DIR}/${FileWoExt}.y
            DEPENDS ${PROJECT_SOURCE_DIR}/lib/cfg-grammar.y
                    ${PROJECT_SOURCE_DIR}/${FileWoExt}.ym)
    elseif(${ARGC} EQUAL 2)
        add_custom_command (OUTPUT ${PROJECT_BINARY_DIR}/${ARGV1}.y
            COMMAND ${PYTHON_EXECUTABLE} ${PROJECT_SOURCE_DIR}/lib/merge-grammar.py ${PROJECT_SOURCE_DIR}/${FileWoExt}.ym > ${PROJECT_BINARY_DIR}/${ARGV1}.y
            DEPENDS ${PROJECT_SOURCE_DIR}/lib/cfg-grammar.y
                    ${PROJECT_SOURCE_DIR}/${FileWoExt}.ym)
    else()
        message(SEND_ERROR "Wrong usage of generate_y_from_ym() function")
    endif()
endfunction()

# This function is used by add_module
function(module_generate_y_from_ym FileWoExtSrc FileWoExtDst)
    if (${ARGC} EQUAL 2 OR ${ARGC} EQUAL 3)
      set(DEPS ${PROJECT_SOURCE_DIR}/lib/cfg-grammar.y)

      if (${ARGC} EQUAL 3)
        set(DEPS ${DEPS} ${ARGV2})
      endif()

      add_custom_command (OUTPUT ${FileWoExtDst}.y
        COMMAND ${PYTHON_EXECUTABLE} ${PROJECT_SOURCE_DIR}/lib/merge-grammar.py ${FileWoExtSrc}.ym > ${FileWoExtDst}.y
            DEPENDS ${DEPS}
            ${FileWoExtSrc}.ym)
    else()
        message(SEND_ERROR "Wrong usage of module_generate_y_from_ym() function")
    endif()
endfunction()

