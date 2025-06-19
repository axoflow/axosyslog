/*
 * Copyright (c) 2017 Balabit
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
#include "stardate.c"
#include "plugin.h"
#include "plugin-types.h"

extern CfgParser stardate_parser;

static Plugin stardate_plugins[] =
{
  TEMPLATE_FUNCTION_PLUGIN(tf_stardate, "stardate"),
};

gboolean
stardate_module_init(PluginContext *context, CfgArgs *args)
{
  plugin_register(context, stardate_plugins, G_N_ELEMENTS(stardate_plugins));
  return TRUE;
}

const ModuleInfo module_info =
{
  .canonical_name = "stardate",
  .version = SYSLOG_NG_VERSION,
  .description = "This function provides stardate template function: "
  "fractional years. Example: $(stardate [--digits 2] $UNIXTIME).",
  .core_revision = SYSLOG_NG_SOURCE_REVISION,
  .plugins = stardate_plugins,
  .plugins_len = G_N_ELEMENTS(stardate_plugins),
};
