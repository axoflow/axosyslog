#############################################################################
# Copyright (c) 2023 Attila Szakacs
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

find_package(PkgConfig)
pkg_check_modules(LIBBPF QUIET libbpf>=1.0.1)

if (LIBBPF_FOUND)
  find_program(BPF_CC clang)
  find_program(BPFTOOL bpftool)

  if ((NOT BPF_CC) OR (NOT BPFTOOL))
    set(LIBBPF_FOUND FALSE)
  else ()
    execute_process(COMMAND ${BPF_CC} -print-file-name=include OUTPUT_VARIABLE CLANG_INCLUDES OUTPUT_STRIP_TRAILING_WHITESPACE)
    pkg_get_variable(LIBBPF_INCLUDE_DIRS_RAW libbpf includedir)
    set(BPF_CFLAGS -nostdinc -isystem ${CLANG_INCLUDES} -target bpf -I${CMAKE_CURRENT_BINARY_DIR} -I${LIBBPF_INCLUDE_DIRS_RAW} -fPIC -O2 -g)
  endif ()
endif ()
