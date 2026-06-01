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
#include <json.h>

static void
_adapt_splunk_response(HttpAdapter *self, HttpResponseSignalData *data)
{
  if (data->http_code >= 400)
    {
      struct json_object *jso = http_adapter_parse_response_json(data->response_body);
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
          http_adapter_locate_offending_payload(data);
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
