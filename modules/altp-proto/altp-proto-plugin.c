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

#include "altp-proto-parser.h"
#include "logproto/logproto-server.h"
#include "plugin.h"
#include "plugin-types.h"

static Plugin altp_proto_plugins[] =
{
  LOG_PROTO_SERVER_PLUGIN_WITH_GRAMMAR(altp_proto_parser, "altp"),
};

gboolean
altp_proto_module_init(PluginContext *context, CfgArgs *args)
{
  plugin_register(context, altp_proto_plugins, G_N_ELEMENTS(altp_proto_plugins));
  return TRUE;
}

const ModuleInfo altp_proto_module_info =
{
  .canonical_name = "altp_proto",
  .version = SYSLOG_NG_VERSION,
  .description =
  "The altp_proto module provides the Receiver side of the Advanced Log Transport Protocol (ALTP).",
  .core_revision = SYSLOG_NG_SOURCE_REVISION,
  .plugins = altp_proto_plugins,
  .plugins_len = G_N_ELEMENTS(altp_proto_plugins),
};
