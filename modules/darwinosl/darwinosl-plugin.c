/*
 * Copyright (c) 2023 Hofi <hofione@gmail.com>
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

#include "messages.h"
#include "cfg-parser.h"
#include "plugin.h"
#include "plugin-types.h"

extern CfgParser darwinosl_parser;

static Plugin darwinosl_plugins[] =
{
  {
    .type = LL_CONTEXT_SOURCE,
    .name = "darwinosl",
    .parser = &darwinosl_parser,
  },
};

gboolean
darwinosl_module_init(PluginContext *context, CfgArgs *args)
{
  plugin_register(context, darwinosl_plugins, G_N_ELEMENTS(darwinosl_plugins));
  return TRUE;
}

const ModuleInfo *sng_module_get_info(void)
{
  static const ModuleInfo info =
  {
    .canonical_name = "darwinosl",
    .version = SYSLOG_NG_VERSION,
    .description = "The darwinosl module provides Darwin OSLog source drivers for syslog-ng where it is available.",
    .core_revision = SYSLOG_NG_SOURCE_REVISION,
    .plugins = darwinosl_plugins,
    .plugins_len = G_N_ELEMENTS(darwinosl_plugins),
  };

  return &info;
}
