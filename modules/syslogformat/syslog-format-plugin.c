/*
 * Copyright (c) 2002-2012 Balabit
 * Copyright (c) 1998-2012 Balázs Scheidler
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

#include "syslog-format.h"
#include "syslog-parser-parser.h"
#include "messages.h"
#include "plugin.h"
#include "plugin-types.h"
#include "filterx-func-format-syslog.h"
#include "filterx/expr-function.h"

static MsgFormatHandler syslog_handler =
{
  .parse = &syslog_format_handler
};

static gpointer
syslog_format_construct(Plugin *self)
{
  return (gpointer) &syslog_handler;
}

static Plugin syslog_format_plugins[] =
{
  {
    .type = LL_CONTEXT_FORMAT,
    .name = "syslog",
    .construct = syslog_format_construct,
  },
  {
    .type = LL_CONTEXT_PARSER,
    .name = "syslog-parser",
    .parser = &syslog_parser_parser,
  },
  {
    .type = LL_CONTEXT_PARSER,
    .name = "sdata-parser",
    .parser = &syslog_parser_parser,
  },
  FILTERX_FUNCTION_PLUGIN(format_syslog_5424),
};

gboolean
syslogformat_module_init(PluginContext *context, CfgArgs *args)
{
  syslog_format_init();
  plugin_register(context, syslog_format_plugins, G_N_ELEMENTS(syslog_format_plugins));
  return TRUE;
}

const ModuleInfo *sng_module_get_info(void)
{
  static const ModuleInfo info =
  {
    .canonical_name = "syslogformat",
    .version = SYSLOG_NG_VERSION,
    .description = "The syslogformat module provides support for parsing RFC3164 and RFC5424 format syslog messages.",
    .core_revision = SYSLOG_NG_SOURCE_REVISION,
    .plugins = syslog_format_plugins,
    .plugins_len = G_N_ELEMENTS(syslog_format_plugins),
  };

  return &info;
}
