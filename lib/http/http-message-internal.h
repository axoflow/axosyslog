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

/* low-level API for the parser and proto */
void http_message_append_body(HTTPMessage *self, const guint8 *data, gsize length);
void http_request_append_url(HTTPRequest *self, const gchar *data, gsize length);
void http_message_add_header_normalized_in_place(HTTPMessage *self, GString *key, GString *value);
GByteArray *http_response_generate_raw_response(HTTPResponse *self);
void http_response_add_mandatory_headers(HTTPResponse *self);
