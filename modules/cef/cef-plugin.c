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
 */

#include "format-cef-extension.h"
#include "plugin.h"
#include "plugin-types.h"
#include "filterx-func-parse-cef.h"
#include "filterx-func-parse-leef.h"
#include "filterx-func-format-cef.h"
#include "filterx-func-format-leef.h"
#include "filterx/expr-function.h"

static Plugin cef_plugins[] =
{
  TEMPLATE_FUNCTION_PLUGIN(tf_cef, "format-cef-extension"),
  FILTERX_GENERATOR_FUNCTION_PLUGIN(parse_cef),
  FILTERX_GENERATOR_FUNCTION_PLUGIN(parse_leef),
  FILTERX_FUNCTION_PLUGIN(format_cef),
  FILTERX_FUNCTION_PLUGIN(format_leef),
};

gboolean
cef_module_init(PluginContext *context, CfgArgs *args)
{
  plugin_register(context, cef_plugins, G_N_ELEMENTS(cef_plugins));
  return TRUE;
}

const ModuleInfo module_info =
{
  .canonical_name = "cef",
  .version = SYSLOG_NG_VERSION,
  .description = "The CEF module provides CEF formatting support for syslog-ng.",
  .core_revision = SYSLOG_NG_SOURCE_REVISION,
  .plugins = cef_plugins,
  .plugins_len = G_N_ELEMENTS(cef_plugins),
};
