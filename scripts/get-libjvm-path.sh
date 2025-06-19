#!/bin/sh
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

recursive_readlink() {
  local READLINK_TARGET="$1"
  while test -L "$READLINK_TARGET"; do
    local READLINK_TARGET=$(readlink "$READLINK_TARGET")
  done
  echo "$READLINK_TARGET"
}

JAVAC_BIN="`which javac`"
JAVAC_BIN="`recursive_readlink "$JAVAC_BIN"`"
if test -e "$JAVA_HOME_CHECKER"; then
  JNI_HOME=`$JAVA_HOME_CHECKER`
else
  JNI_HOME=`echo $JAVAC_BIN | sed "s~/bin/javac$~/~"`
fi
JNI_LIBDIR=`find $JNI_HOME \( -name "libjvm.so" -or -name "libjvm.dylib" \) \
        | sed "s-/libjvm\.so-/-" \
        | sed "s-/libjvm\.dylib-/-" | head -n 1`
