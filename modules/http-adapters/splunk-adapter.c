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
#include "compat/string.h"
#include <json.h>

static json_object *
_parse_response_json(GString *response)
{
  struct json_object *jso;
  struct json_tokener *tok;

  tok = json_tokener_new();
  jso = json_tokener_parse_ex(tok, response->str, response->len);
  if (tok->err != json_tokener_success || !jso)
    {
      msg_error("http-response-adapter(): failed to parse JSON response",
                evt_tag_str("input", response->str),
                tok->err != json_tokener_success ? evt_tag_str ("json_error", json_tokener_error_desc(tok->err)) : NULL);
      json_tokener_free (tok);
      return NULL;
    }
  json_tokener_free(tok);
  return jso;
}

static void
_locate_offending_payload(HttpResponseSignalData *data)
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
  return;
}

static void
_adapt_splunk_response(HttpAdapter *self, HttpResponseSignalData *data)
{
  if (data->http_code >= 400)
    {
      struct json_object *jso = _parse_response_json(data->response_body);
      if (jso)
        {
          struct json_object *text_jso = NULL;
          struct json_object *event_num_jso = NULL;
          if (!json_object_object_get_ex(jso, "text", &text_jso))
            goto exit;
          if (!json_object_object_get_ex(jso, "invalid-event-number", &event_num_jso))
            goto exit;
          data->offending_message = json_object_get_int(event_num_jso);
          if (data->offending_message >= data->batch_size)
            data->offending_message = 0;
          _locate_offending_payload(data);
exit:
          json_object_put(jso);
        }
    }
}


HttpAdapter *
splunk_adapter_new(void)
{
  HttpAdapter *self = g_new0(HttpAdapter, 1);
  http_adapter_init_instance(self);
  self->adapt_response = _adapt_splunk_response;
  return self;
}
