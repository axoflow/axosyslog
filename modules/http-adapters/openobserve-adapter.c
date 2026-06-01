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
#include "openobserve-adapter.h"
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
                tok->err != json_tokener_success ? evt_tag_str("json_error", json_tokener_error_desc(tok->err)) : NULL);
      json_tokener_free(tok);
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
}

/* OpenObserve returns HTTP 200 for partial batch failures, with a JSON body of the form:
 *   {"code":200,"status":[{"name":"<stream>","successful":N,"failed":M,"error":"<reason>"},...]}
 *
 * The adapter detects failures in the status array and identifies the first failing message
 * by assuming messages are processed in order: the first failure is at index total_successful.
 */
static void
_adapt_openobserve_response(HttpAdapter *self, HttpResponseSignalData *data)
{
  if (data->http_code != 200)
    return;

  struct json_object *jso = _parse_response_json(data->response_body);
  if (!jso)
    return;

  struct json_object *status_jso = NULL;
  if (!json_object_object_get_ex(jso, "status", &status_jso))
    goto exit;

  if (!json_object_is_type(status_jso, json_type_array))
    goto exit;

  gint total_successful = 0;
  gint total_failed = 0;
  const gchar *first_error = NULL;

  gint num_entries = json_object_array_length(status_jso);
  for (gint i = 0; i < num_entries; i++)
    {
      struct json_object *entry = json_object_array_get_idx(status_jso, i);
      struct json_object *successful_jso = NULL;
      struct json_object *failed_jso = NULL;
      struct json_object *error_jso = NULL;

      json_object_object_get_ex(entry, "successful", &successful_jso);
      json_object_object_get_ex(entry, "failed", &failed_jso);
      json_object_object_get_ex(entry, "error", &error_jso);

      if (successful_jso)
        total_successful += json_object_get_int(successful_jso);
      if (failed_jso)
        total_failed += json_object_get_int(failed_jso);
      if (error_jso && !first_error)
        first_error = json_object_get_string(error_jso);
    }

  if (total_failed > 0)
    {
      msg_notice("openobserve: partial batch failure reported by server",
                 evt_tag_int("successful", total_successful),
                 evt_tag_int("failed", total_failed),
                 first_error ? evt_tag_str("error", first_error) : NULL);
      data->offending_message = (guint) total_successful;
      if (data->offending_message >= data->batch_size)
        data->offending_message = 0;
      _locate_offending_payload(data);
    }

exit:
  json_object_put(jso);
}


HttpAdapter *
openobserve_adapter_new(void)
{
  HttpAdapter *self = g_new0(HttpAdapter, 1);
  http_adapter_init_instance(self);
  self->adapt_response = _adapt_openobserve_response;
  return self;
}
