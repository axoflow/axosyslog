/*
 * Copyright (c) 2023 László Várady
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
#include "protos/apphook.h"

extern CfgParser bigquery_parser;

static Plugin bigquery_plugins[] =
{
  {
    .type = LL_CONTEXT_DESTINATION,
    .name = "bigquery",
    .parser = &bigquery_parser,
  },
};

gboolean
bigquery_module_init(PluginContext *context, CfgArgs *args)
{
  plugin_register(context, bigquery_plugins, G_N_ELEMENTS(bigquery_plugins));
  grpc_register_global_initializers();
  return TRUE;
}

const ModuleInfo *sng_module_get_info(void)
{
  static const ModuleInfo info =
  {
    .canonical_name = "bigquery",
    .version = SYSLOG_NG_VERSION,
    .description = "Google Cloud BigQuery plugins",
    .core_revision = SYSLOG_NG_SOURCE_REVISION,
    .plugins = bigquery_plugins,
    .plugins_len = G_N_ELEMENTS(bigquery_plugins),
  };

  return &info;
}
