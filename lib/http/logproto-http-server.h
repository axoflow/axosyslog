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

#ifndef LOGPROTO_HTTP_SERVER_H_INCLUDED
#define LOGPROTO_HTTP_SERVER_H_INCLUDED

#include "logproto/logproto-server.h"
#include "http/http-message.h"

#include <glib.h>

typedef struct _LogProtoHTTPServer LogProtoHTTPServer;

typedef GQueue *(*LPHTTPExtractLogMessagesFunc)(HTTPRequest *http_request, gpointer user_data);
typedef HTTPResponse *(*LPHTTPCreateResponseFunc)(HTTPRequest *http_request, gpointer user_data);

LogProtoServer *log_proto_http_server_new(LogTransport *transport, const LogProtoServerOptions *options);
void log_proto_http_server_set_extract_log_messages(LogProtoServer *self,
                                                    LPHTTPExtractLogMessagesFunc extract_messages, gpointer user_data);
void log_proto_http_server_set_create_response(LogProtoServer *self, LPHTTPCreateResponseFunc create_response,
                                               gpointer user_data);

#endif
