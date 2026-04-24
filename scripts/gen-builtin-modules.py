#!/usr/bin/env python3
#############################################################################
# Copyright (c) 2026 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

import sys
import pathlib


builtin_modules_skeleton = """
#include "plugin.h"

{extern_module_infos}

void
register_builtin_modules(PluginContext *context)
{{
{register_module_infos}
}}
"""

def _extract_module_name(lib):
    plib = pathlib.Path(lib)
    if plib.stem.startswith('lib'):
      return plib.stem[3:]
    else:
      return plib.stem

def _extract_normalized_module_name(lib):
    return _extract_module_name(lib).translate(str.maketrans('-','_'))

def _generate_declaration(static_libs):
    for lib in static_libs:
        yield f"extern ModuleInfo {_extract_normalized_module_name(lib)}_module_info;"

def _generate_registration(static_libs):
    for lib in static_libs:
        yield f"""  plugin_extract_candidate_plugins(context, "{_extract_module_name(lib)}", &{_extract_normalized_module_name(lib)}_module_info);"""


def main():
    static_libs = sys.argv[1:]

    print(builtin_modules_skeleton.format(
      extern_module_infos="\n".join(_generate_declaration(static_libs)),
      register_module_infos="\n".join(_generate_registration(static_libs))))

if __name__ == '__main__':
    main()
