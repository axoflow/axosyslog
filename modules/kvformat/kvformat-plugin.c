/*
 * Copyright (c) 2015 Balabit
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
#include "format-welf.h"
#include "filterx-func-parse-kv.h"
#include "filterx-func-format-kv.h"
#include "filterx/expr-function.h"

extern CfgParser kv_parser_parser;

static Plugin kvformat_plugins[] =
{
  {
    .type = LL_CONTEXT_PARSER,
    .name = "kv-parser",
    .parser = &kv_parser_parser,
  },
  {
    .type = LL_CONTEXT_PARSER,
    .name = "linux-audit-parser",
    .parser = &kv_parser_parser,
  },
  TEMPLATE_FUNCTION_PLUGIN(tf_format_welf, "format-welf"),
  FILTERX_GENERATOR_FUNCTION_PLUGIN(parse_kv),
  FILTERX_FUNCTION_PLUGIN(format_kv),
};

gboolean
kvformat_module_init(PluginContext *context, CfgArgs *args)
{
  plugin_register(context, kvformat_plugins, G_N_ELEMENTS(kvformat_plugins));
  return TRUE;
}

const ModuleInfo module_info =
{
  .canonical_name = "kvformat",
  .version = SYSLOG_NG_VERSION,
  .description = "The kvformat module provides key-value format (such as WELF) support for syslog-ng.",
  .core_revision = SYSLOG_NG_SOURCE_REVISION,
  .plugins = kvformat_plugins,
  .plugins_len = G_N_ELEMENTS(kvformat_plugins),
};
