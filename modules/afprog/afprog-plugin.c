/*
 * Copyright (c) 2002-2011 Balabit
 * Copyright (c) 1998-2011 Balázs Scheidler
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#include "cfg-parser.h"
#include "plugin.h"
#include "plugin-types.h"

extern CfgParser afprog_parser;

static Plugin afprog_plugins[] =
{
  {
    .type = LL_CONTEXT_SOURCE,
    .name = "program",
    .parser = &afprog_parser,
  },
  {
    .type = LL_CONTEXT_DESTINATION,
    .name = "program",
    .parser = &afprog_parser,
  },
};

gboolean
afprog_module_init(PluginContext *context, CfgArgs *args)
{
  plugin_register(context, afprog_plugins, G_N_ELEMENTS(afprog_plugins));
  return TRUE;
}

const ModuleInfo *sng_module_get_info(void)
{
  static const ModuleInfo info =
  {
    .canonical_name = "afprog",
    .version = SYSLOG_NG_VERSION,
    .description = "The afprog module provides program source & destination drivers for syslog-ng.",
    .core_revision = SYSLOG_NG_SOURCE_REVISION,
    .plugins = afprog_plugins,
    .plugins_len = G_N_ELEMENTS(afprog_plugins),
  };

  return &info;
}
