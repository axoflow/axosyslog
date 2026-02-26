/*
 * Copyright (c) 2021 One Identity
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
#include "plugin.h"
#include "plugin-types.h"
#include <MQTTClient.h>

extern CfgParser mqtt_parser;

static Plugin mqtt_plugins[] =
{
  {
    .type = LL_CONTEXT_DESTINATION,
    .name = "mqtt",
    .parser = &mqtt_parser
  },
  {
    .type = LL_CONTEXT_SOURCE,
    .name = "mqtt",
    .parser = &mqtt_parser
  }
};

static void
_mqtt_internal_log(enum MQTTCLIENT_TRACE_LEVELS level, gchar *message)
{
  if (level >= MQTTCLIENT_TRACE_ERROR)
    {
      msg_error("MQTT error", evt_tag_str("error_message", message));
      return;
    }

  msg_trace("MQTT debug", evt_tag_str("message", message));
}

gboolean
mqtt_module_init(PluginContext *context, CfgArgs *args)
{
  plugin_register(context, mqtt_plugins, G_N_ELEMENTS(mqtt_plugins));

  MQTTClient_setTraceCallback(_mqtt_internal_log);
  MQTTClient_setTraceLevel(MQTTCLIENT_TRACE_PROTOCOL);

  return TRUE;
}

const ModuleInfo *sng_module_get_info(void)
{
  static const ModuleInfo info =
  {
    .canonical_name = "mqtt",
    .version = SYSLOG_NG_VERSION,
    .description = "MQTT plugins",
    .core_revision = SYSLOG_NG_SOURCE_REVISION,
    .plugins = mqtt_plugins,
    .plugins_len = G_N_ELEMENTS(mqtt_plugins),
  };

  return &info;
}
