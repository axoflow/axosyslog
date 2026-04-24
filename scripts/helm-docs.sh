#!/usr/bin/env bash
#############################################################################
# Copyright (c) 2026 Axoflow
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
# Generates README.md files for Helm charts using helm-docs.
# Usage: scripts/helm-docs.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

if ! command -v helm-docs &>/dev/null; then
    echo "Error: helm-docs is not installed or not in PATH." >&2
    echo "Install it from: https://github.com/norwoodj/helm-docs" >&2
    exit 1
fi

helm-docs \
    --chart-search-root="$REPO_ROOT/charts" \
    --template-files="$REPO_ROOT/charts/docs/templates/overrides.gotmpl" \
    --template-files="README.md.gotmpl"
