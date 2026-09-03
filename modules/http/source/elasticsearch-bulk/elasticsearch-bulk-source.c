/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Balazs Scheidler <balazs.scheidler@axoflow.com>
 * Copyright (c) 2026 Attila Szakacs-Bertok <attila.szakacs@axoflow.com>
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

#include "elasticsearch-bulk-source.h"
#include "http/http-message.h"
#include "socket/socket-options-inet.h"
#include "logmsg/logmsg.h"
#include "msg-format.h"

#include <string.h>

static const gchar *ES_BULK_OPS[] = { "index", "create", "update", "delete" };

/* the action type is the first key of the action object */
static const gchar *
_action_op(const gchar *action, gsize action_length)
{
  const gchar *key = memchr(action, '"', action_length);
  if (!key)
    return NULL;
  key++;

  const gchar *key_end = memchr(key, '"', action + action_length - key);
  if (!key_end)
    return NULL;

  for (gsize i = 0; i < G_N_ELEMENTS(ES_BULK_OPS); i++)
    {
      gsize op_length = strlen(ES_BULK_OPS[i]);
      if ((gsize) (key_end - key) == op_length && strncmp(key, ES_BULK_OPS[i], op_length) == 0)
        return ES_BULK_OPS[i];
    }
  return NULL;
}

/* bulk clients ack per event: they walk items[] positionally, so every
 * action needs exactly one item */
static void
_append_item(ESBulkSourceConnection *self, const gchar *op, gint status)
{
  GString *items = self->response.items;
  if (items->len)
    g_string_append_c(items, ',');
  g_string_append_printf(items, "{\"%s\":{\"status\":%d}}", op, status);
  if (status >= 300)
    self->response.errors = TRUE;
}

static const gchar *
_next_line(const gchar **data, gsize *remaining, gsize *line_length)
{
  if (!*remaining)
    return NULL;

  const gchar *line = *data;
  const gchar *eol = memchr(line, '\n', *remaining);
  gsize length = eol ? (gsize) (eol - line) : *remaining;

  gsize consumed = eol ? length + 1 : length;
  *data += consumed;
  *remaining -= consumed;

  /* accept both LF and CRLF framing */
  if (length && line[length - 1] == '\r')
    length--;

  *line_length = length;
  return line;
}

static GQueue *
_extract_bulk_pairs(HTTPRequest *http_request, ESBulkSourceConnection *connection, ESBulkSourceDriver *self)
{
  MsgFormatOptions *parse_options = &self->super.super.reader_options.parse_options;

  const GByteArray *body = http_message_get_body(&http_request->super);
  if (!body || !body->data)
    return NULL;

  GQueue *messages = g_queue_new();

  const gchar *data = (const gchar *) body->data;
  gsize remaining = body->len;

  const gchar *action;
  gsize action_length;

  while ((action = _next_line(&data, &remaining, &action_length)))
    {
      /* libbeat separates action/source pairs with a blank line */
      if (action_length == 0)
        continue;

      const gchar *op = _action_op(action, action_length);

      gsize source_length = 0;
      const gchar *source = _next_line(&data, &remaining, &source_length);

      if (source && source_length)
        {
          LogMessage *msg = msg_format_construct_message(parse_options, (guchar *) source, source_length);
          msg_format_parse_into(parse_options, msg, (guchar *) source, &source_length);
          log_msg_set_value(msg, self->handles.elastic_bulk_action, action, action_length);
          g_queue_push_tail(messages, msg);
          _append_item(connection, op ? op : "index", 201);
        }
      else
        {
          _append_item(connection, op ? op : "index", 400);
        }
    }
  return messages;
}

static GQueue *
_extract_log_messages(HTTPRequest *http_request, HTTPSourceConnection *connection)
{
  ESBulkSourceDriver *self = (ESBulkSourceDriver *) connection->owner;
  ESBulkSourceConnection *es_connection = (ESBulkSourceConnection *) connection;

  g_string_truncate(es_connection->response.items, 0);
  es_connection->response.errors = FALSE;

  return _extract_bulk_pairs(http_request, es_connection, self);
}

static HTTPResponse *
_new_response(HTTPStatusCode status_code)
{
  HTTPResponse *response = http_response_new_empty();
  http_message_set_http_version(&response->super, 1, 1);
  http_response_set_status_code(response, status_code);
  return response;
}

static void
_take_json_body(HTTPResponse *response, GString *body_string)
{
  http_message_add_header(&response->super, "content-type", "application/json");

  gsize length = body_string->len;
  GByteArray *body = g_byte_array_new_take((guint8 *) g_string_free(body_string, FALSE), length);
  http_message_take_body(&response->super, body);
}

static HTTPResponse *
_create_bulk_ack_response(ESBulkSourceConnection *es_connection)
{
  HTTPResponse *response = _new_response(HTTP_OK);

  GString *items = es_connection->response.items;
  GString *body = g_string_sized_new(items->len + 40);
  g_string_append_printf(body, "{\"took\":0,\"errors\":%s,\"items\":[",
                         es_connection->response.errors ? "true" : "false");
  g_string_append_len(body, items->str, items->len);
  g_string_append(body, "]}");

  _take_json_body(response, body);
  return response;
}

static HTTPResponse *
_create_response(HTTPRequest *http_request, HTTPSourceConnection *connection)
{
  ESBulkSourceConnection *es_connection = (ESBulkSourceConnection *) connection;

  return _create_bulk_ack_response(es_connection);
}

static void
_sc_free(LogPipe *s)
{
  ESBulkSourceConnection *self = (ESBulkSourceConnection *) s;

  g_string_free(self->response.items, TRUE);
  afsocket_sc_free_method(s);
}

static AFSocketSourceConnection *
_construct_connection(AFSocketSourceDriver *s, GSockAddr *peer_addr, GSockAddr *local_addr, gint fd)
{
  ESBulkSourceConnection *self = g_new0(ESBulkSourceConnection, 1);

  afsocket_sc_init_instance(&self->super, peer_addr, local_addr, fd, s->super.super.super.cfg);
  self->super.super.free_fn = _sc_free;
  self->response.items = g_string_sized_new(256);

  return &self->super;
}

static gboolean
_sd_init(LogPipe *s)
{
  ESBulkSourceDriver *self = (ESBulkSourceDriver *) s;

  self->super.extract_log_messages = _extract_log_messages;

  return http_sd_init_method(s);
}

ESBulkSourceDriver *
elasticsearch_bulk_sd_new(GlobalConfig *cfg)
{
  ESBulkSourceDriver *self = g_new0(ESBulkSourceDriver, 1);
  http_sd_init_instance(&self->super, socket_options_inet_new(), http_transport_mapper_new(), cfg);
  /* the bulk source lines are JSON documents, not syslog messages */
  self->super.super.reader_options.parse_options.flags |= LP_NOPARSE;

  self->handles.elastic_bulk_action = log_msg_get_value_handle(".es_bulk.action");

  self->super.create_response = _create_response;
  self->super.super.construct_connection = _construct_connection;
  self->super.super.super.super.super.init = _sd_init;

  return self;
}
