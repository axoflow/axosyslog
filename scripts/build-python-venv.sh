#!/bin/sh
#############################################################################
# Copyright (c) 2022 Balazs Scheidler <bazsi77@gmail.com>
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

# script to generate the virtualenv to be used during syslog-ng build (e.g.
# it contains all dev requirements as well as runtime requirements)

if [ "$1" = "" ]; then
    echo "build-python-venv: <path to python> <python-virtualenv> <path to syslog-ng source>"
    echo "$@"
    exit 1
fi

PYTHON=$1
PYTHON_VENV_DIR=$2
top_srcdir=$3
REQUIREMENTS_FILE=${top_srcdir}/dev-requirements.txt
OPTIONAL_REQUIREMENTS_FILE=${top_srcdir}/optional-dev-requirements.txt

set -e

echo "Building dev virtualenv for syslog-ng at ${PYTHON_VENV_DIR} from ${REQUIREMENTS_FILE} and ${OPTIONAL_REQUIREMENTS_FILE}"

rm -rf ${PYTHON_VENV_DIR}
${PYTHON} -m venv ${PYTHON_VENV_DIR}
${PYTHON_VENV_DIR}/bin/python -m pip install --upgrade pip
${PYTHON_VENV_DIR}/bin/python -m pip install --upgrade setuptools
${PYTHON_VENV_DIR}/bin/python -m pip install -r $REQUIREMENTS_FILE
${PYTHON_VENV_DIR}/bin/python -m pip install -r $OPTIONAL_REQUIREMENTS_FILE || echo "Some optional pip packages were not installed. Continuing..."
