/*
 * Copyright (c) 2018 Balabit
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

#ifndef HTTP_SOURCE_H_INCLUDED
#define HTTP_SOURCE_H_INCLUDED

#include "syslog-ng.h"
#include "socket/afsocket-source.h"
#include "http/http-message.h"
#include "transport/tls-context.h"

typedef struct _HTTPSourceDriver HTTPSourceDriver;
typedef AFSocketSourceConnection HTTPSourceConnection;

struct _HTTPSourceDriver
{
  AFSocketSourceDriver super;

  gchar *bind_port;
  gchar *bind_ip;

  GQueue *(*extract_log_messages)(HTTPRequest *http_request, HTTPSourceConnection *connection);
  HTTPResponse *(*create_response)(HTTPRequest *http_request, HTTPSourceConnection *connection);
};

TransportMapper *http_transport_mapper_new(void);

void http_sd_set_tls_context(LogDriver *s, TLSContext *tls_context);
void http_sd_set_localport(LogDriver *self, gchar *service);
void http_sd_set_localip(LogDriver *self, gchar *ip);

void http_sd_init_instance(HTTPSourceDriver *self, SocketOptions *socket_options,
                           TransportMapper *transport_mapper, GlobalConfig *cfg);
gboolean http_sd_init_method(LogPipe *s);
void http_sd_free_method(LogPipe *self);

#endif
