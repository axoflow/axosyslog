/*
 * Copyright (c) 2026 Axoflow
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

#include "http-source-parser.h"
#include "plugin.h"
#include "plugin-types.h"

static Plugin http_source_plugins[] =
{
  {
    .type = LL_CONTEXT_SOURCE,
    .name = "http",
    .parser = &http_source_parser,
  },
};

gboolean
http_source_module_init(PluginContext *context, CfgArgs *args)
{
  plugin_register(context, http_source_plugins, G_N_ELEMENTS(http_source_plugins));
  return TRUE;
}

const ModuleInfo http_source_module_info =
{
  .canonical_name = "http-source",
  .version = SYSLOG_NG_VERSION,
  .description = "The http-source module provides a generic HTTP server source for syslog-ng.",
  .core_revision = SYSLOG_NG_SOURCE_REVISION,
  .plugins = http_source_plugins,
  .plugins_len = G_N_ELEMENTS(http_source_plugins),
};
