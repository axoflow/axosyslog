/*
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

#include "http-adapter.h"
#include "splunk-adapter.h"
#include "openobserve-adapter.h"
#include "compat/string.h"

#define HTTP_ADAPTER_PLUGIN "http-adapter"

json_object *
http_adapter_parse_response_json(GString *response)
{
  struct json_object *jso;
  struct json_tokener *tok;

  tok = json_tokener_new();
  jso = json_tokener_parse_ex(tok, response->str, response->len);
  if (tok->err != json_tokener_success || !jso)
    {
      msg_error("http-response-adapter(): failed to parse JSON response",
                evt_tag_str("input", response->str),
                tok->err != json_tokener_success ? evt_tag_str("json_error", json_tokener_error_desc(tok->err)) : NULL);
      json_tokener_free(tok);
      return NULL;
    }
  json_tokener_free(tok);
  return jso;
}

void
http_adapter_locate_offending_payload(HttpResponseSignalData *data)
{
  const gchar *line = data->request_body->str;

  for (gint i = 0; i < data->offending_message; i++)
    {
      const gchar *nl = strchr(line, '\n');
      if (!nl)
        goto notfound;
      line = nl + 1;
    }
  data->offending_request_len = strchrnul(line, '\n') - line;
  if (data->offending_request_len != 0)
    {
      data->offending_request_start = line - data->request_body->str;
      return;
    }

notfound:
  data->offending_request_start = data->offending_request_len = 0;
}

static void
_on_http_response_received(HttpAdapter *self, HttpResponseSignalData *data)
{
  http_adapter_adapt_response(self, data);
}

static gboolean
_attach(LogDriverPlugin *s, LogDriver *driver)
{
  HttpAdapter *self = (HttpAdapter *) s;

  SignalSlotConnector *ssc = driver->signal_slot_connector;
  CONNECT(ssc, signal_http_response, _on_http_response_received, self);

  return TRUE;
}

static void
_detach(LogDriverPlugin *s, LogDriver *driver)
{
  HttpAdapter *self = (HttpAdapter *) s;
  SignalSlotConnector *ssc = driver->signal_slot_connector;

  DISCONNECT(ssc, signal_http_response, _on_http_response_received, self);
}

void
http_adapter_init_instance(HttpAdapter *self)
{
  log_driver_plugin_init_instance(&(self->super), HTTP_ADAPTER_PLUGIN);
  self->super.attach = _attach;
  self->super.detach = _detach;
}

HttpAdapter *
http_adapter_new_by_name(const gchar *name)
{
  if (strcmp(name, "splunk") == 0)
    return splunk_adapter_new();
  if (strcmp(name, "openobserve") == 0)
    return openobserve_adapter_new();
  return NULL;
}
