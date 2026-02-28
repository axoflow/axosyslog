/*
 * Copyright (c) 2017 Balabit
 * Copyright (c) 2017 Balazs Scheidler <balazs.scheidler@balabit.com>
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

#include "plugin.h"
#include "plugin-types.h"

extern CfgParser map_value_pairs_parser;

static Plugin map_value_pairs_plugins[] =
{
  {
    .type = LL_CONTEXT_PARSER,
    .name = "map_value_pairs",
    .parser = &map_value_pairs_parser,
  },
};

gboolean
map_value_pairs_module_init(PluginContext *context, CfgArgs *args G_GNUC_UNUSED)
{
  plugin_register(context, map_value_pairs_plugins, G_N_ELEMENTS(map_value_pairs_plugins));
  return TRUE;
}

const ModuleInfo *sng_module_get_info(void)
{
  static const ModuleInfo info =
  {
    .canonical_name = "map-value-pairs",
    .version = SYSLOG_NG_VERSION,
    .description = "The map-names module provides the map-names() parser for syslog-ng.",
    .core_revision = SYSLOG_NG_SOURCE_REVISION,
    .plugins = map_value_pairs_plugins,
    .plugins_len = G_N_ELEMENTS(map_value_pairs_plugins),
  };

  return &info;
}
