#############################################################################
# Copyright (c) 2026 Axoflow
# Copyright (c) 2026 Attila Szakacs-Bertok <attila.szakacs@axoflow.com>
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

# The fuzzing engine supplies main(), so these are plain executables and are
# never registered as ctest cases.

# Harnesses that only need the core library.
set(FILTERX_FUZZERS
  fuzz_parse_json
  fuzz_regexp
  fuzz_decode
  fuzz_typecast
  fuzz_str
  fuzz_digest
  fuzz_strptime
  fuzz_repr
)

foreach(fuzzer ${FILTERX_FUZZERS})
  add_executable(${fuzzer} ${fuzzer}.c)
  target_link_libraries(${fuzzer} PRIVATE syslog-ng ${LIB_FUZZING_ENGINE})
endforeach()

if (ENABLE_XML)
  add_executable(fuzz_parse_xml fuzz_parse_xml.c)
  target_link_libraries(fuzz_parse_xml PRIVATE syslog-ng xml ${LIB_FUZZING_ENGINE})
  target_include_directories(fuzz_parse_xml PRIVATE ${PROJECT_SOURCE_DIR}/modules/xml)
endif()

add_executable(fuzz_parse_cef fuzz_parse_cef.c)
target_link_libraries(fuzz_parse_cef PRIVATE syslog-ng cef ${LIB_FUZZING_ENGINE})
target_include_directories(fuzz_parse_cef PRIVATE ${PROJECT_SOURCE_DIR}/modules/cef)

add_executable(fuzz_parse_kv fuzz_parse_kv.c)
target_link_libraries(fuzz_parse_kv PRIVATE syslog-ng kvformat ${LIB_FUZZING_ENGINE})
target_include_directories(fuzz_parse_kv PRIVATE ${PROJECT_SOURCE_DIR}/modules/kvformat)

add_executable(fuzz_parse_csv fuzz_parse_csv.c)
target_link_libraries(fuzz_parse_csv PRIVATE syslog-ng csvparser ${LIB_FUZZING_ENGINE})
target_include_directories(fuzz_parse_csv PRIVATE ${PROJECT_SOURCE_DIR}/modules/csvparser)

# Keep the linker from dropping the module libraries whose constructors we call directly.
if (NOT APPLE)
  foreach(fuzzer fuzz_parse_xml fuzz_parse_cef fuzz_parse_kv fuzz_parse_csv)
    set_property(TARGET ${fuzzer} APPEND_STRING PROPERTY LINK_FLAGS " -Wl,--no-as-needed")
  endforeach()
endif()
