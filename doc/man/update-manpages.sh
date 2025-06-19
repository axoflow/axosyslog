#!/usr/bin/env bash
#############################################################################
# Copyright (c) 2017 Balabit
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

GUIDE_REPO=syslog-ng-ose-guides

PROJECT_DIR=$(dirname $(realpath ${BASH_SOURCE}))

set -e

echo -e "\e[33;1mDownload admin guide\e[0m"
cd $PROJECT_DIR
rm -rf $GUIDE_REPO
git clone git@github.com:balabit/${GUIDE_REPO}.git
cd $GUIDE_REPO/en
echo -e "\e[33;1mBuild profiled XML source files\e[0m"
make manpages

echo -e "\e[33;1mUpdate manpage source files\e[0m"
cd $PROJECT_DIR
rm *.xml
for new_file in $GUIDE_REPO/en/out/tmp/man/*.profiled.xml; do
    original=$(basename ${new_file/.profiled/})
    echo "Updating file '$original'"
    cp "$new_file" "./$original"
done

rm -rf $GUIDE_REPO

echo -e "\e[33;1mDONE.\e[0m"

git status
