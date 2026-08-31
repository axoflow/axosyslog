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

#ifndef HTTP_PARSER_H_INCLUDED
#define HTTP_PARSER_H_INCLUDED

#include "http-message.h"

#include <glib.h>

#define HTTP_PARSER_ERROR http_parser_error_quark()
GQuark http_parser_error_quark(void);

typedef struct _HTTPParser HTTPParser;

HTTPParser *http_request_parser_new(void);
HTTPParser *http_response_parser_new(void);
void http_parser_free(HTTPParser *self);

gboolean http_parser_feed(HTTPParser *self, const gchar *data, gsize length, gsize *consumed_bytes);
gboolean http_parser_signal_end_of_stream(HTTPParser *self);

void http_parser_skip_message(HTTPParser *self);
gboolean http_parser_is_message_complete(const HTTPParser *self);
HTTPMessage *http_parser_steal_message(HTTPParser *self);

GError *http_parser_get_last_error(const HTTPParser *self);

#endif
