/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Attila Szakacs <attila.szakacs@axoflow.com>
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

extern CfgParser arrow_flight_parser;

static Plugin arrow_flight_plugins[] =
{
  {
    .type = LL_CONTEXT_DESTINATION,
    .name = "arrow-flight",
    .parser = &arrow_flight_parser,
  },
};

gboolean
arrow_flight_module_init(PluginContext *context, CfgArgs *args)
{
  plugin_register(context, arrow_flight_plugins, G_N_ELEMENTS(arrow_flight_plugins));
  return TRUE;
}

const ModuleInfo arrow_flight_module_info =
{
  .canonical_name = "arrow-flight",
  .version = SYSLOG_NG_VERSION,
  .description = "Apache Arrow Flight destination",
  .core_revision = SYSLOG_NG_SOURCE_REVISION,
  .plugins = arrow_flight_plugins,
  .plugins_len = G_N_ELEMENTS(arrow_flight_plugins),
};
