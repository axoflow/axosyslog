/*
 * Copyright (c) 2015 Balabit
 * Copyright (c) 2015 Vincent Bernat <Vincent.Bernat@exoscale.ch>
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

#include "timestamp-parser.h"
#include "tf-format-date.h"

#include "plugin.h"
#include "plugin-types.h"

extern CfgParser timestamp_parser;

static Plugin timestamp_plugins[] =
{
  {
    .type = LL_CONTEXT_PARSER,
    .name = "date-parser",
    .parser = &timestamp_parser,
  },
  {
    .type = LL_CONTEXT_REWRITE,
    .name = "fix-time-zone",
    .parser = &timestamp_parser,
  },
  {
    .type = LL_CONTEXT_REWRITE,
    .name = "set-time-zone",
    .parser = &timestamp_parser,
  },
  {
    .type = LL_CONTEXT_REWRITE,
    .name = "guess-time-zone",
    .parser = &timestamp_parser,
  },
  TEMPLATE_FUNCTION_PLUGIN(tf_format_date, "format-date"),
};

gboolean
timestamp_module_init(PluginContext *context, CfgArgs *args G_GNUC_UNUSED)
{
  plugin_register(context, timestamp_plugins, G_N_ELEMENTS(timestamp_plugins));
  return TRUE;
}

const ModuleInfo *sng_module_get_info(void)
{
  static const ModuleInfo info =
  {
    .canonical_name = "timestamp",
    .version = SYSLOG_NG_VERSION,
    .description = "The timestamp module provides support for manipulating timestamps in syslog-ng.",
    .core_revision = SYSLOG_NG_SOURCE_REVISION,
    .plugins = timestamp_plugins,
    .plugins_len = G_N_ELEMENTS(timestamp_plugins),
  };

  return &info;
}
