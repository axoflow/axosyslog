/*
 * Copyright (c) 2017 Balabit
 * Copyright (c) 2017 László Várady
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

#include "http-parser.h"
#include "http-message-internal.h"
#include "llhttp/include/llhttp.h"

#define HPE_UPGRADE_NOT_SUPPORTED 501

struct _HTTPParser
{
  llhttp_t parser;
  llhttp_settings_t parser_settings;
  llhttp_errno_t finish_error;

  HTTPMessage *current_message;
  gboolean message_complete;

  GString *current_header_field_name;
  GString *current_header_field_value;
};

static gint _message_begin(llhttp_t *parser);
static gint _headers_complete(llhttp_t *parser);
static gint _message_complete(llhttp_t *parser);
static gint _url(llhttp_t *parser, const gchar *data, gsize length);
static gint _header_field(llhttp_t *parser, const gchar *data, gsize length);
static gint _header_value(llhttp_t *parser, const gchar *data, gsize length);
static gint _body(llhttp_t *parser, const gchar *data, gsize length);

GQuark
http_parser_error_quark(void)
{
  return g_quark_from_static_string("http-parser-error-quark");
}

static HTTPParser *
_http_parser_new(llhttp_type_t parser_type)
{
  HTTPParser *self = g_new0(HTTPParser, 1);

  self->parser_settings = (llhttp_settings_t)
  {
    .on_message_begin = _message_begin,
    .on_url = _url,
    .on_status = NULL,
    .on_header_field = _header_field,
    .on_header_value = _header_value,
    .on_headers_complete = _headers_complete,
    .on_body = _body,
    .on_message_complete = _message_complete,
    .on_chunk_header = NULL,
    .on_chunk_complete = NULL,
  };

  llhttp_init(&self->parser, parser_type, &self->parser_settings);
  self->parser.data = self;

  self->current_header_field_name = g_string_sized_new(32);
  self->current_header_field_value = g_string_sized_new(32);

  return self;
}

HTTPParser *
http_request_parser_new(void)
{
  return _http_parser_new(HTTP_REQUEST);
}

HTTPParser *
http_response_parser_new(void)
{
  return _http_parser_new(HTTP_RESPONSE);
}

void
http_parser_free(HTTPParser *self)
{
  http_message_free(self->current_message);
  g_string_free(self->current_header_field_name, TRUE);
  g_string_free(self->current_header_field_value, TRUE);
  g_free(self);
}

static HTTPMessage *
_create_message(llhttp_type_t type)
{
  switch (type)
    {
    case HTTP_REQUEST:
      return &http_request_new_empty()->super;
    case HTTP_RESPONSE:
      return &http_response_new_empty()->super;
    default:
      g_assert_not_reached();
    }
}

gboolean
http_parser_feed(HTTPParser *self, const gchar *data, gsize length, gsize *consumed_bytes)
{
  if (llhttp_get_errno(&self->parser) == HPE_PAUSED)
    {
      *consumed_bytes = 0;
      return TRUE;
    }

  llhttp_errno_t error = llhttp_execute(&self->parser, data, length);

  if (error == HPE_OK)
    *consumed_bytes = length;
  else
    *consumed_bytes = llhttp_get_error_pos(&self->parser) - data;

  /* upgrade is not supported */
  if (self->parser.upgrade)
    return FALSE;

  /* pause is not an error */
  if (error != HPE_OK && error != HPE_PAUSED)
    return FALSE;

  return TRUE;
}

gboolean
http_parser_signal_end_of_stream(HTTPParser *self)
{
  self->finish_error = llhttp_finish(&self->parser);
  return self->finish_error == HPE_OK;
}

void
http_parser_skip_message(HTTPParser *self)
{
  http_message_free(self->current_message);
  self->current_message = NULL;
  self->message_complete = FALSE;
  self->finish_error = HPE_OK;
  llhttp_reset(&self->parser);
}

gboolean
http_parser_is_message_complete(const HTTPParser *self)
{
  return self->message_complete;
}

HTTPMessage *
http_parser_steal_message(HTTPParser *self)
{
  if (!self->message_complete)
    return NULL;

  HTTPMessage *msg = self->current_message;
  self->current_message = NULL;
  self->message_complete = FALSE;
  self->finish_error = HPE_OK;
  llhttp_reset(&self->parser);

  return msg;
}

GError *
http_parser_get_last_error(const HTTPParser *self)
{
  llhttp_errno_t error = llhttp_get_errno(&self->parser);
  if (error == HPE_OK)
    error = self->finish_error;

  if (error != HPE_OK)
    return g_error_new(HTTP_PARSER_ERROR, error, "%s",
                       llhttp_get_error_reason(&self->parser) ? : llhttp_errno_name(error));

  if (self->parser.upgrade)
    return g_error_new(HTTP_PARSER_ERROR, HPE_UPGRADE_NOT_SUPPORTED, "HTTP upgrade is not supported");

  return NULL;
}

static gboolean
http_parser_previous_header_exists(HTTPParser *self)
{
  return self->current_header_field_value->len != 0 && self->current_header_field_name->len != 0;
}

static void
http_parser_reset_header(HTTPParser *self)
{
  g_string_truncate(self->current_header_field_name, 0);
  g_string_truncate(self->current_header_field_value, 0);
}

static void
http_parser_finalize_previous_header(HTTPParser *self)
{
  if (!http_parser_previous_header_exists(self))
    return;

  http_message_add_header_normalized_in_place(self->current_message,
                                              self->current_header_field_name, self->current_header_field_value);

  http_parser_reset_header(self);
}

static gint
_message_begin(llhttp_t *parser)
{
  HTTPParser *self = parser->data;

  if (!self->current_message)
    self->current_message = _create_message(self->parser.type);

  return 0;
}

static gint
_headers_complete(llhttp_t *parser)
{
  HTTPParser *self = parser->data;

  http_parser_finalize_previous_header(self);

  if (parser->type == HTTP_RESPONSE)
    {
      HTTPResponse *response = (HTTPResponse *) self->current_message;
      http_response_set_status_code(response, parser->status_code);
    }

  if (parser->type == HTTP_REQUEST)
    {
      HTTPRequest *request = (HTTPRequest *) self->current_message;
      http_request_set_method(request, llhttp_method_name(parser->method));
    }

  http_message_set_http_version(self->current_message, parser->http_major, parser->http_minor);
  return 0;
}

static gint
_message_complete(llhttp_t *parser)
{
  HTTPParser *self = parser->data;

  self->message_complete = TRUE;
  return HPE_PAUSED;
}

static gint
_url(llhttp_t *parser, const gchar *data, gsize length)
{
  HTTPParser *self = parser->data;
  HTTPRequest *request = (HTTPRequest *) self->current_message;

  http_request_append_url(request, data, length);
  return 0;
}

static gint
_header_field(llhttp_t *parser, const gchar *data, gsize length)
{
  HTTPParser *self = parser->data;

  http_parser_finalize_previous_header(self);

  g_string_append_len(self->current_header_field_name, data, length);
  return 0;
}

static gint
_header_value(llhttp_t *parser, const gchar *data, gsize length)
{
  HTTPParser *self = parser->data;

  g_string_append_len(self->current_header_field_value, data, length);
  return 0;
}

static gint
_body(llhttp_t *parser, const gchar *data, gsize length)
{
  HTTPParser *self = parser->data;

  http_message_append_body(self->current_message, (guint8 *) data, length);
  return 0;
}
