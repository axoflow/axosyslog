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

#ifndef HTTP_MESSAGE_H_INCLUDED
#define HTTP_MESSAGE_H_INCLUDED

#include "http-status.h"

#include <glib.h>

typedef struct _HTTPMessage HTTPMessage;
struct _HTTPMessage
{
  gushort http_major;
  gushort http_minor;

  GString *raw_headers;
  GHashTable *header_positions;

  GByteArray *body;

  void (*free)(HTTPMessage *self);
};

void http_message_set_http_version(HTTPMessage *self, gushort major, gushort minor);
void http_message_get_http_version(HTTPMessage *self, gushort *major, gushort *minor);
void http_message_add_header(HTTPMessage *self, const gchar *key, const gchar *value);
GString *http_message_get_header(HTTPMessage *self, const gchar *key);
GString *http_message_get_header_using_normalized_key(HTTPMessage *self, const gchar *normalized_key);
gboolean http_message_header_exists(HTTPMessage *self, const gchar *key);
gboolean http_message_normalized_header_exists(HTTPMessage *self, const gchar *normalized_key);
void http_message_take_body(HTTPMessage *self, GByteArray *body);
const GByteArray *http_message_get_body(HTTPMessage *self);
void http_message_free(HTTPMessage *self);


typedef struct _HTTPRequest
{
  HTTPMessage super;
  GString *url;
  gchar *method;
} HTTPRequest;

HTTPRequest *http_request_new_empty(void);
void http_request_set_url(HTTPRequest *self, const gchar *url);
const gchar *http_request_get_url(HTTPRequest *self);
void http_request_set_method(HTTPRequest *self, const gchar *method);
const gchar *http_request_get_method(HTTPRequest *self);
void http_request_free(HTTPRequest *self);

void http_request_null_terminate_body(HTTPRequest *self);


typedef struct _HTTPResponse
{
  HTTPMessage super;
  HTTPStatusCode status_code;
} HTTPResponse;

HTTPResponse *http_response_new_empty(void);
void http_response_set_status_code(HTTPResponse *self, HTTPStatusCode status_code);
HTTPStatusCode http_response_get_status_code(HTTPResponse *self);
void http_response_free(HTTPResponse *self);

const gchar *http_response_status_code_to_status_line(HTTPStatusCode code);

#endif
