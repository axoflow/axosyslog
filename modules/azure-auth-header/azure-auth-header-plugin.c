/*
 * Copyright (c) 2020 One Identity
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

extern CfgParser azure_auth_header_parser;

static Plugin azure_auth_header_plugins[] =
{
  {
    .type = LL_CONTEXT_INNER_DEST,
    .name = "azure-auth-header",
    .parser = &azure_auth_header_parser,
  },
};

gboolean
azure_auth_header_module_init(PluginContext *context, CfgArgs *args)
{
  plugin_register(context, azure_auth_header_plugins, G_N_ELEMENTS(azure_auth_header_plugins));
  return TRUE;
}

const ModuleInfo *sng_module_get_info(void)
{
  static const ModuleInfo info =
  {
    .canonical_name = "azure_auth_header",
    .version = SYSLOG_NG_VERSION,
    .description = "HTTP authentication header support for Azure APIs",
    .core_revision = SYSLOG_NG_SOURCE_REVISION,
    .plugins = azure_auth_header_plugins,
    .plugins_len = G_N_ELEMENTS(azure_auth_header_plugins),
  };

  return &info;
}
