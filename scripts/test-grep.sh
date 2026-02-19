#!/bin/bash
#############################################################################
# Copyright (c) 2020 Balabit
# Copyright (c) 2020 Kokan <kokaipeter@gmail.com>
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

# TODO fix them
sanitizer_exception_list=(
  "modules/python/tests/test_python_persist"
  "modules/python/tests/test_python_template"
  "modules/python/tests/test_python_persist_name"
  "modules/python/tests/test_python_logmsg"
  "modules/correlation/tests/test_patterndb"
  "modules/secure-logging/tests/test_secure_logging"
  "modules/afmongodb/tests/test-mongodb-config"
  "modules/grpc/otel/tests/test_otel_protobuf_parser"
)

on_exception_list() {
  for suffix in "${sanitizer_exception_list[@]}"; do
    [[ "$1" == *"$suffix" ]] && return 0
  done

  return 1
}

$1 > $1.result 2>&1
exit_status=$?
# Put error messages into test-suite.log (as automake does)
# on passed cases, output is not redirected into test-suite.log
cat $1.result

if ! on_exception_list "$1" && egrep -q 'ERROR: (LeakSanitizer|AddressSanitizer)' $1.result; then
  echo "SAN report detected"
  exit 1
fi

# keep only result file if test failed
if test $exit_status -eq 0 ; then
  rm -f -- $1.result
fi

exit $exit_status
