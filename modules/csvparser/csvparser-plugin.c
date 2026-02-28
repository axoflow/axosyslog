/*
 * Copyright (c) 2002-2015 Balabit
 * Copyright (c) 1998-2015 Balázs Scheidler
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
#include "csvparser.h"
#include "plugin.h"
#include "plugin-types.h"
#include "filterx-func-parse-csv.h"
#include "filterx-func-format-csv.h"
#include "filterx/expr-function.h"

extern CfgParser csvparser_parser;

static Plugin csvparser_plugins[] =
{
  {
    .type = LL_CONTEXT_PARSER,
    .name = "csv-parser",
    .parser = &csvparser_parser,
  },
  FILTERX_FUNCTION_PLUGIN(parse_csv),
  FILTERX_FUNCTION_PLUGIN(format_csv),
};

gboolean
csvparser_module_init(PluginContext *context, CfgArgs *args)
{
  plugin_register(context, csvparser_plugins, G_N_ELEMENTS(csvparser_plugins));
  return TRUE;
}

const ModuleInfo *sng_module_get_info(void)
{
  static const ModuleInfo info =
  {
    .canonical_name = "csvparser",
    .version = SYSLOG_NG_VERSION,
    .description = "The csvparser module provides parsing support for CSV and other separated value formats for syslog-ng.",
    .core_revision = SYSLOG_NG_SOURCE_REVISION,
    .plugins = csvparser_plugins,
    .plugins_len = G_N_ELEMENTS(csvparser_plugins),
  };

  return &info;
}
