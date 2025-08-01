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

echo "Removing extra NULL sentinel after msg_error etc. macros in $PWD" >&2

find . -iname .git -prune -false -o -type f -exec \
 sed --regexp-extended --silent --in-place --expression "
     1h
     1! H
     $ {
      g
     s~(\<msg_(fatal|error|warning|notice|info|progress|verbose|debug|trace|warning_once)\
\s*\([^;]*),\s*NULL\s*\)\s*[;]~\1);~g
     p
    }
    " {} +
