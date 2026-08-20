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

#include "http-message.h"
#include "http-message-internal.h"

#include <string.h>

#define INITIAL_RAW_HEADERS_BUFFER_SIZE 64
#define INITIAL_URL_BUFFER_SIZE 64
#define HTTP_HEADER_KEY_VALUE_SEPARATOR ": "
#define HTTP_CRLF "\r\n"
#define HTTP_HEADER_SEPARATOR HTTP_CRLF

typedef struct _HTTPHeaderKey
{
  const gchar **buffer;
  gsize offset;
  gsize length;
} HTTPHeaderKey;

typedef struct _HTTPHeaderValuePosition
{
  gsize offset;
  gsize length;
} HTTPHeaderValuePosition;

static const gchar *
http_header_get_key_start(const HTTPHeaderKey *key)
{
  return (*key->buffer) + key->offset;
}

static gsize
http_header_get_key_length(const HTTPHeaderKey *key)
{
  return key->length;
}

static guint
_http_header_key_hash(gconstpointer k)
{
  const HTTPHeaderKey *key = k;
  guint32 hash = 5381;

  const signed char *skey = (const signed char *) http_header_get_key_start(key);
  for (gsize i = 0; i < key->length; ++i)
    hash = (hash << 5) + hash + skey[i];

  return hash;
}

static gboolean
_http_header_key_equal(gconstpointer k1, gconstpointer k2)
{
  const HTTPHeaderKey *key1 = k1;
  const HTTPHeaderKey *key2 = k2;

  if (http_header_get_key_length(key1) != http_header_get_key_length(key2))
    return FALSE;

  return strncmp(http_header_get_key_start(key1), http_header_get_key_start(key2),
                 http_header_get_key_length(key1)) == 0;
}


static void
http_message_new_method(HTTPMessage *self)
{
  self->raw_headers = g_string_sized_new(INITIAL_RAW_HEADERS_BUFFER_SIZE);
  self->header_positions = g_hash_table_new_full(_http_header_key_hash, _http_header_key_equal, g_free, g_free);
  self->body = g_byte_array_new();
}

static void
http_message_free_method(HTTPMessage *self)
{
  g_string_free(self->raw_headers, TRUE);
  g_hash_table_destroy(self->header_positions);
  g_byte_array_free(self->body, TRUE);
  g_free(self);
}

void
http_message_set_http_version(HTTPMessage *self, gushort major, gushort minor)
{
  self->http_major = major;
  self->http_minor = minor;
}

void
http_message_get_http_version(HTTPMessage *self, gushort *major, gushort *minor)
{
  *major = self->http_major;
  *minor = self->http_minor;
}

static const HTTPHeaderValuePosition *
http_message_get_header_value_position(HTTPMessage *self, const gchar *normalized_key)
{
  HTTPHeaderKey key_wrapper =
  {
    .buffer = &normalized_key,
    .offset = 0,
    .length = strlen(normalized_key)
  };

  return g_hash_table_lookup(self->header_positions, &key_wrapper);
}

GString *
http_message_get_header_using_normalized_key(HTTPMessage *self, const gchar *normalized_key)
{
  const HTTPHeaderValuePosition *value_position = http_message_get_header_value_position(self, normalized_key);
  if (!value_position)
    return NULL;

  return g_string_new_len(self->raw_headers->str + value_position->offset, value_position->length);
}

GString *
http_message_get_header(HTTPMessage *self, const gchar *key)
{
  gchar *normalized_key = g_ascii_strdown(key, strlen(key));
  GString *value = http_message_get_header_using_normalized_key(self, normalized_key);
  g_free(normalized_key);

  return value;
}

static void
http_message_add_normalized_header(HTTPMessage *self, const gchar *key, const gchar *value)
{
  HTTPHeaderKey *header_key = g_new(HTTPHeaderKey, 1);
  header_key->buffer = (const gchar **) &self->raw_headers->str;
  header_key->offset = self->raw_headers->len;

  g_string_append(self->raw_headers, key);

  header_key->length = self->raw_headers->len - header_key->offset;

  g_string_append(self->raw_headers, HTTP_HEADER_KEY_VALUE_SEPARATOR);

  HTTPHeaderValuePosition *value_position = g_new(HTTPHeaderValuePosition, 1);
  value_position->offset = self->raw_headers->len;

  g_string_append(self->raw_headers, value);

  value_position->length = self->raw_headers->len - value_position->offset;

  g_string_append(self->raw_headers, HTTP_HEADER_SEPARATOR);

  g_hash_table_insert(self->header_positions, header_key, value_position);
}

void
http_message_add_header(HTTPMessage *self, const gchar *key, const gchar *value)
{
  gchar *normalized_key = g_ascii_strdown(key, strlen(key));
  http_message_add_normalized_header(self, normalized_key, value);
  g_free(normalized_key);
}

gboolean
http_message_normalized_header_exists(HTTPMessage *self, const gchar *normalized_key)
{
  return http_message_get_header_value_position(self, normalized_key) != NULL;
}

gboolean
http_message_header_exists(HTTPMessage *self, const gchar *key)
{
  gchar *normalized_key = g_ascii_strdown(key, strlen(key));
  gboolean exists = http_message_normalized_header_exists(self, normalized_key);
  g_free(normalized_key);

  return exists;
}

void
http_message_take_body(HTTPMessage *self, GByteArray *body)
{
  g_byte_array_free(self->body, TRUE);
  self->body = body;
}

const GByteArray *
http_message_get_body(HTTPMessage *self)
{
  return self->body;
}

void
http_message_free(HTTPMessage *self)
{
  if (self)
    self->free(self);
}

void
http_message_append_body(HTTPMessage *self, const guint8 *data, gsize length)
{
  g_byte_array_append(self->body, data, length);
}

void
http_message_add_header_normalized_in_place(HTTPMessage *self, GString *key, GString *value)
{
  g_string_ascii_down(key);
  http_message_add_normalized_header(self, key->str, value->str);
}


/* HTTPRequest */

static void
http_request_free_method(HTTPMessage *s)
{
  HTTPRequest *self = (HTTPRequest *) s;
  g_string_free(self->url, TRUE);
  g_free(self->method);
  http_message_free_method(s);
}

HTTPRequest *
http_request_new_empty(void)
{
  HTTPRequest *self = g_new0(HTTPRequest, 1);

  http_message_new_method(&self->super);
  self->super.free = http_request_free_method;

  self->url = g_string_sized_new(INITIAL_URL_BUFFER_SIZE);

  return self;
}

void
http_request_set_url(HTTPRequest *self, const gchar *url)
{
  g_string_assign(self->url, url);
}

const gchar *
http_request_get_url(HTTPRequest *self)
{
  return self->url->str;
}

void
http_request_set_method(HTTPRequest *self, const gchar *method)
{
  g_free(self->method);
  self->method = g_strdup(method);
}

const gchar *
http_request_get_method(HTTPRequest *self)
{
  return self->method;
}

void
http_request_free(HTTPRequest *self)
{
  if (self)
    http_message_free(&self->super);
}

void
http_request_append_url(HTTPRequest *self, const gchar *data, gsize length)
{
  g_string_append_len(self->url, data, length);
}

void
http_request_null_terminate_body(HTTPRequest *self)
{
  self->super.body = g_byte_array_set_size(self->super.body, self->super.body->len + 1);
  self->super.body->data[self->super.body->len - 1] = 0;
}


/* HTTPResponse */

static void
http_response_free_method(HTTPMessage *s)
{
  http_message_free_method(s);
}

HTTPResponse *
http_response_new_empty(void)
{
  HTTPResponse *self = g_new0(HTTPResponse, 1);

  http_message_new_method(&self->super);
  self->super.free = http_response_free_method;

  return self;
}

void
http_response_set_status_code(HTTPResponse *self, HTTPStatusCode status_code)
{
  self->status_code = status_code;
}

HTTPStatusCode
http_response_get_status_code(HTTPResponse *self)
{
  return self->status_code;
}

void
http_response_free(HTTPResponse *self)
{
  if (self)
    http_message_free(&self->super);
}


/*
 * "200 OK",
 * "404 Not Found",
 * ...
 */
static const gchar *http_status_lines_200[] =
{
#define T(code, name, reason_phrase) #code " " #reason_phrase,
  HTTP_STATUS_MAP_200(T)
#undef T
};

static const gchar *http_status_lines_300[] =
{
#define T(code, name, reason_phrase) #code " " #reason_phrase,
  HTTP_STATUS_MAP_300(T)
#undef T
};

static const gchar *http_status_lines_400[] =
{
#define T(code, name, reason_phrase) #code " " #reason_phrase,
  HTTP_STATUS_MAP_400(T)
#undef T
};

static const gchar *http_status_lines_500[] =
{
#define T(code, name, reason_phrase) #code " " #reason_phrase,
  HTTP_STATUS_MAP_500(T)
#undef T
};

const gchar *
http_response_status_code_to_status_line(HTTPStatusCode code)
{
  gsize status_lines_array_size;
  const gchar **status_lines;
  gsize offset;

  if (code < HTTP_OK)
    return NULL;
  else if (code < HTTP_MULTIPLE_CHOICES)
    {
      status_lines = http_status_lines_200;
      status_lines_array_size = G_N_ELEMENTS(http_status_lines_200);
      offset = 200;
    }
  else if (code < HTTP_BAD_REQUEST)
    {
      status_lines = http_status_lines_300;
      status_lines_array_size = G_N_ELEMENTS(http_status_lines_300);
      offset = 300;
    }
  else if (code < HTTP_INTERNAL_SERVER_ERROR)
    {
      status_lines = http_status_lines_400;
      status_lines_array_size = G_N_ELEMENTS(http_status_lines_400);
      offset = 400;
    }
  else
    {
      status_lines = http_status_lines_500;
      status_lines_array_size = G_N_ELEMENTS(http_status_lines_500);
      offset = 500;
    }

  gsize status_line_index = code - offset;
  if (status_line_index >= status_lines_array_size)
    return NULL;

  return status_lines[status_line_index];
}

GByteArray *
http_response_generate_raw_response(HTTPResponse *self)
{
  GByteArray *raw_response = g_byte_array_sized_new(self->super.body->len + self->super.raw_headers->len + 64);

  {
    gsize http_version_length = 9;
    gchar http_version[http_version_length + 1];
    g_snprintf(http_version, http_version_length + 1, "HTTP/%u.%u ", self->super.http_major, self->super.http_minor);
    g_byte_array_append(raw_response, (const guint8 *) http_version, http_version_length);
  }

  const gchar *status_line = http_response_status_code_to_status_line(self->status_code);
  gsize status_line_length = strlen(status_line);

  g_byte_array_append(raw_response, (const guint8 *) status_line, status_line_length);
  g_byte_array_append(raw_response, (const guint8 *) HTTP_CRLF, sizeof(HTTP_CRLF) - 1);
  g_byte_array_append(raw_response, (const guint8 *) self->super.raw_headers->str, self->super.raw_headers->len);
  g_byte_array_append(raw_response, (const guint8 *) HTTP_CRLF, sizeof(HTTP_CRLF) - 1);
  g_byte_array_append(raw_response, self->super.body->data, self->super.body->len);

  return raw_response;
}

void http_response_add_mandatory_headers(HTTPResponse *self)
{
  if (!http_message_normalized_header_exists(&self->super, "content-length"))
    {
      gsize body_length = http_message_get_body(&self->super) ? http_message_get_body(&self->super)->len : 0;
      gchar content_length[32];
      g_snprintf(content_length, sizeof(content_length), "%zu", body_length);

      http_message_add_normalized_header(&self->super, "content-length", content_length);
    }

  if (!http_message_normalized_header_exists(&self->super, "server"))
    http_message_add_normalized_header(&self->super, "server", "syslog-ng");
}
