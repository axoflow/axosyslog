/*
 * Copyright (c) 2018 Balabit
 * Copyright (c) 2018 László Várady <laszlo.varady@balabit.com>
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

#include "http-source.h"
#include "http/http-message.h"
#include "http/http-message-internal.h"
#include "socket/socket-options-inet.h"
#include "logmsg/logmsg.h"
#include "msg-format.h"

#include <string.h>
#include <strings.h>

#include <json.h>

typedef GQueue *(*EHTTPExtractMessageFunc)(HTTPRequest *http_request, HTTPSourceConnection *connection);

GQueue *
_extract_single_message(HTTPRequest *http_request, HTTPSourceConnection *connection)
{
  EHTTPSourceDriver *self = (EHTTPSourceDriver *) connection->owner;
  MsgFormatOptions *parse_options = &self->super.super.reader_options.parse_options;

  const GByteArray *body = http_message_get_body(&http_request->super);
  if (!body || !body->data)
    return NULL;

  GQueue *messages = g_queue_new();

  const gchar *message = (const gchar *) body->data;
  LogMessage *msg = msg_format_construct_message(parse_options, (guchar *) message, body->len);
  gsize message_length = body->len;
  msg_format_parse_into(parse_options, msg, (guchar *) message, &message_length);

  g_queue_push_tail(messages, msg);

  return messages;
}

GQueue *
_extract_messages_line_separated(HTTPRequest *http_request, HTTPSourceConnection *connection)
{
  EHTTPSourceDriver *self = (EHTTPSourceDriver *) connection->owner;
  MsgFormatOptions *parse_options = &self->super.super.reader_options.parse_options;

  http_request_null_terminate_body(http_request);
  const GByteArray *body = http_message_get_body(&http_request->super);
  if (!body || !body->data)
    return NULL;

  GQueue *messages = g_queue_new();

  gchar *state;
  gchar *data = (gchar *) body->data;
  gchar *line = strtok_r(data, "\n", &state);

  while (line)
    {
      gsize line_length = strlen(line);
      LogMessage *msg = msg_format_construct_message(parse_options, (guchar *) line, line_length);
      msg_format_parse_into(parse_options, msg, (guchar *) line, &line_length);
      g_queue_push_tail(messages, msg);

      line = strtok_r(NULL, "\n", &state);
    }
  return messages;
}

static GQueue *
_extract_from_json(struct json_object *obj, GSockAddr *saddr, MsgFormatOptions *parse_options)
{
  struct json_object *messages_obj = obj;

  if (!json_object_is_type(obj, json_type_array))
    {
      struct json_object_iter itr;
      json_object_object_foreachC(obj, itr)
      {
        if (json_object_is_type(itr.val, json_type_array))
          {
            messages_obj = itr.val;
            break;
          }
      }

      if (messages_obj == obj)
        {
          msg_warning("Error extracting JSON messages, array object is not found");
          return NULL;
        }
    }

  GQueue *messages = g_queue_new();

  gsize messages_size = json_object_array_length(messages_obj);
  for (gsize i = 0; i < messages_size; ++i)
    {
      struct json_object *message_obj = json_object_array_get_idx(messages_obj, i);
      const gchar *message = json_object_get_string(message_obj);

      gsize message_length = strlen(message);
      LogMessage *msg = msg_format_construct_message(parse_options, (guchar *) message, message_length);
      msg_format_parse_into(parse_options, msg, (guchar *) message, &message_length);
      g_queue_push_tail(messages, msg);
    }

  return messages;
}

GQueue *
_extract_messages_json_array(HTTPRequest *http_request, HTTPSourceConnection *connection)
{
  EHTTPSourceDriver *self = (EHTTPSourceDriver *) connection->owner;
  MsgFormatOptions *parse_option = &self->super.super.reader_options.parse_options;

  const GByteArray *body = http_message_get_body(&http_request->super);
  if (!body || !body->data)
    return NULL;

  struct json_tokener *tok = json_tokener_new();
  struct json_object *obj = json_tokener_parse_ex(tok, (const gchar *) body->data, body->len);
  if (tok->err != json_tokener_success || !obj)
    {
      msg_warning("Error parsing JSON messages");
      json_tokener_free(tok);
      return NULL;
    }
  json_tokener_free(tok);

  GQueue *messages = _extract_from_json(obj, connection->peer_addr, parse_option);
  json_object_put(obj);

  return messages;
}

/* EHTTPSourceMode ordering */
static EHTTPExtractMessageFunc ehttp_extract_modes[] =
{
  _extract_single_message,
  _extract_messages_line_separated,
  _extract_messages_json_array
};

static gboolean
_authenticate(EHTTPSourceDriver *self, HTTPRequest *http_request)
{
  if (!self->auth_token || self->auth_token[0] == '\0')
    return TRUE;

  GString *authorization = http_message_get_header(&http_request->super, "Authorization");
  if (!authorization)
    {
      msg_debug("Auth failed, missing Authorization header");
      return FALSE;
    }

  gboolean result = strcmp(authorization->str, self->auth_token) == 0;

  g_string_free(authorization, TRUE);
  return result;
}

static GQueue *
_extract_log_messages(HTTPRequest *http_request, HTTPSourceConnection *connection)
{
  EHTTPSourceDriver *self = (EHTTPSourceDriver *) connection->owner;

  if (!_authenticate(self, http_request))
    return NULL;

  GQueue *messages = ehttp_extract_modes[self->mode](http_request, connection);

  LogMessage *first_message = messages ? g_queue_peek_head(messages) : NULL;
  if (self->response_body && first_message)
    ((EHTTPSourceConnection *) connection)->first_message = log_msg_ref(first_message);

  return messages;
}

HTTPResponse *
_create_response(HTTPRequest *http_request, HTTPSourceConnection *connection)
{
  EHTTPSourceDriver *self = (EHTTPSourceDriver *) connection->owner;

  if (!_authenticate(self, http_request))
    {
      HTTPResponse *response = http_response_new_empty();
      http_message_set_http_version(&response->super, 1, 1);
      http_response_set_status_code(response, HTTP_FORBIDDEN);
      return response;
    }

  HTTPResponse *http_response = http_response_new_empty();
  http_message_set_http_version(&http_response->super, 1, 1);
  http_response_set_status_code(http_response, HTTP_OK);

  GByteArray* body = g_byte_array_sized_new(32);

  if (self->response_body)
    {
      EHTTPSourceConnection *ehttp_connection = (EHTTPSourceConnection *) connection;

      GString *formatted = g_string_sized_new(64);
      LogMessage *msg = ehttp_connection->first_message ? ehttp_connection->first_message : log_msg_new_empty();
      ehttp_connection->first_message = NULL;
      log_template_format(self->response_body, msg, &DEFAULT_TEMPLATE_EVAL_OPTIONS, formatted);
      log_msg_unref(msg);

      g_byte_array_append(body, (const guint8 *) formatted->str, formatted->len);
      g_string_free(formatted, TRUE);
    }
  else
    {
      const gchar *status_line = http_response_status_code_to_status_line(HTTP_OK);
      g_byte_array_append(body, (const guint8 *) status_line, strlen(status_line));
    }

  http_message_take_body(&http_response->super, body);

  return http_response;
}

gboolean
ehttp_sd_set_mode(LogDriver *d, const gchar *mode)
{
  EHTTPSourceDriver *self = (EHTTPSourceDriver *) d;

  if (strcasecmp(mode, "single") == 0)
    self->mode = EHTTP_SINGLE;
  else if (strcasecmp(mode, "line-separated") == 0 || strcasecmp(mode, "jsonl") == 0)
    self->mode = EHTTP_LINE_SEPARATED;
  else if (strcasecmp(mode, "json-array") == 0)
    self->mode = EHTTP_JSON_ARRAY;
  else
    return FALSE;

  return TRUE;
}

void
ehttp_sd_set_auth_token(LogDriver *d, const gchar *auth_token)
{
  EHTTPSourceDriver *self = (EHTTPSourceDriver *) d;

  g_free(self->auth_token);
  self->auth_token = g_strdup(auth_token);
}

void
ehttp_sd_set_response_body(LogDriver *d, LogTemplate *response_body)
{
  EHTTPSourceDriver *self = (EHTTPSourceDriver *) d;

  log_template_unref(self->response_body);
  self->response_body = log_template_ref(response_body);
}

static void
ehttp_sc_free(LogPipe *s)
{
  EHTTPSourceConnection *self = (EHTTPSourceConnection *) s;

  if (self->first_message)
    log_msg_unref(self->first_message);
  afsocket_sc_free_method(s);
}

static AFSocketSourceConnection *
ehttp_sd_construct_connection(AFSocketSourceDriver *s, GSockAddr *peer_addr, GSockAddr *local_addr, gint fd)
{
  EHTTPSourceConnection *self = g_new0(EHTTPSourceConnection, 1);

  afsocket_sc_init_instance(&self->super, peer_addr, local_addr, fd, s->super.super.super.cfg);
  self->super.super.free_fn = ehttp_sc_free;

  return &self->super;
}

gboolean
ehttp_sd_init(LogPipe *s)
{
  EHTTPSourceDriver *self = (EHTTPSourceDriver *) s;

  self->super.extract_log_messages = _extract_log_messages;

  return http_sd_init_method(s);
}

static void
ehttp_sd_free(LogPipe *s)
{
  EHTTPSourceDriver *self = (EHTTPSourceDriver *) s;

  g_free(self->auth_token);
  log_template_unref(self->response_body);
  http_sd_free_method(s);
}

EHTTPSourceDriver *
ehttp_sd_new(GlobalConfig *cfg)
{
  EHTTPSourceDriver *self = g_new0(EHTTPSourceDriver, 1);
  http_sd_init_instance(&self->super, socket_options_inet_new(), http_transport_mapper_new(), cfg);

  self->mode = EHTTP_SINGLE;

  self->super.create_response = _create_response;
  self->super.super.construct_connection = ehttp_sd_construct_connection;
  self->super.super.super.super.super.init = ehttp_sd_init;
  self->super.super.super.super.super.free_fn = ehttp_sd_free;

  return self;
}
