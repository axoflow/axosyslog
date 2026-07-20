#!/usr/bin/env bash
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
#
# Verify that every tracked tests/light/**/*.py file is enumerated in the
# EXTRA_DIST list of its Makefile.am.  These lists are hand-maintained but must
# stay in sync with the tree, otherwise the e2e tests silently drop out of the
# source distribution (they still run in CI, which works from the git checkout,
# so the gap is invisible until someone builds from the dist tarball).

set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

status=0

check_dir() {
    local dir="$1"
    local makefile="$dir/Makefile.am"

    local tracked listed diff_out
    tracked=$(git ls-files "$dir" | grep '\.py$' | sort)
    listed=$(grep -oE "$dir/[^[:space:]]*\.py" "$makefile" | sort -u)

    if ! diff_out=$(diff <(printf '%s\n' "$tracked") <(printf '%s\n' "$listed")); then
        echo "EXTRA_DIST in $makefile is out of sync with the tree:"
        printf '%s\n' "$diff_out" | sed -n 's/^< /  missing from EXTRA_DIST: /p; s/^> /  stale in EXTRA_DIST:     /p'
        status=1
    fi
}

check_dir tests/light/functional_tests
check_dir tests/light/src/axosyslog_light

if [ "$status" -ne 0 ]; then
    echo
    echo "Regenerate the list with the recipe at the top of the Makefile.am, e.g.:"
    echo "  (cd tests/light/functional_tests && find -name '*.py' | awk '{ print \"\\ttests/light/functional_tests/\"substr(\$0,3) }' | sort)"
    exit 1
fi
