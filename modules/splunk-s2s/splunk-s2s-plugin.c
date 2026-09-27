/*
 * Copyright (c) 2026 Adam Kiss
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

#include "splunk-s2s-proto-client.h"

DEFINE_LOG_PROTO_CLIENT(log_proto_splunk_s2s, .default_inet_port = 9997);

static Plugin splunk_s2s_plugins[] =
{
  LOG_PROTO_CLIENT_PLUGIN(log_proto_splunk_s2s, "splunk-s2s"),
};

gboolean
splunk_s2s_module_init(PluginContext *context, CfgArgs *args)
{
  plugin_register(context, splunk_s2s_plugins, G_N_ELEMENTS(splunk_s2s_plugins));
  return TRUE;
}

const ModuleInfo splunk_s2s_module_info =
{
  .canonical_name = "splunk-s2s",
  .version = SYSLOG_NG_VERSION,
  .description = "The splunk-s2s module provides the Splunk S2S (cooked mode) transport for syslog-ng.",
  .core_revision = SYSLOG_NG_SOURCE_REVISION,
  .plugins = splunk_s2s_plugins,
  .plugins_len = G_N_ELEMENTS(splunk_s2s_plugins),
};
