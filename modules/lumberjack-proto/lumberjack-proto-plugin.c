/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#include "lumberjack-proto-parser.h"
#include "logproto/logproto-server.h"
#include "plugin.h"
#include "plugin-types.h"

static Plugin lumberjack_proto_plugins[] =
{
  LOG_PROTO_SERVER_PLUGIN_WITH_GRAMMAR(lumberjack_proto_parser, "lumberjack"),
};

gboolean
lumberjack_proto_module_init(PluginContext *context, CfgArgs *args)
{
  plugin_register(context, lumberjack_proto_plugins, G_N_ELEMENTS(lumberjack_proto_plugins));
  return TRUE;
}

const ModuleInfo lumberjack_proto_module_info =
{
  .canonical_name = "lumberjack_proto",
  .version = SYSLOG_NG_VERSION,
  .description =
  "The lumberjack_proto module provides the receiver side of the Lumberjack (Beats) protocol, "
  "versions 1 and 2, as transport(lumberjack()) of the network() source driver.",
  .core_revision = SYSLOG_NG_SOURCE_REVISION,
  .plugins = lumberjack_proto_plugins,
  .plugins_len = G_N_ELEMENTS(lumberjack_proto_plugins),
};
