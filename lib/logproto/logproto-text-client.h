/*
 * Copyright (c) 2002-2012 Balabit
 * Copyright (c) 1998-2012 Balázs Scheidler
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
#ifndef LOGPROTO_TEXT_CLIENT_H_INCLUDED
#define LOGPROTO_TEXT_CLIENT_H_INCLUDED

#include "logproto-client.h"

typedef struct _LogProtoTextClient
{
  LogProtoClient super;

  /* messages accepted by post() but not acked yet, stored back-to-back so
   * that a single write() sends all of them */
  struct
  {
    GString *data;
    gsize written;
    gsize *msg_lens;
    gint size;
    gint count;
    /* the messages at the front that have been written completely */
    gint acked;
    gsize acked_len;
    /* a write left data behind: accept nothing until it is written, a
     * blocked SSL_write() must be repeated with the same buffer */
    gboolean draining;
  } batch;
} LogProtoTextClient;

LogProtoStatus log_proto_text_client_post_with_prefix(LogProtoClient *s, const guchar *prefix, gsize prefix_len,
                                                      const guchar *msg, gsize msg_len, gboolean *consumed);
LogProtoStatus log_proto_text_client_flush_method(LogProtoClient *s);
LogProtoStatus log_proto_text_client_post_method(LogProtoClient *s, LogMessage *logmsg, const guchar *msg,
                                                 gsize msg_len, gboolean *consumed);
void log_proto_text_client_init(LogProtoTextClient *self, LogTransport *transport,
                                const LogProtoClientOptions *options,
                                gboolean batching_supported);
void log_proto_text_client_set_batch_size(LogProtoTextClient *self, gint batch_size);
LogProtoClient *log_proto_text_client_new(LogTransport *transport, const LogProtoClientOptions *options);

LogProtoClient *log_proto_unidirectional_text_client_new(LogTransport *transport, const LogProtoClientOptions *options);

void log_proto_text_client_free_method(LogProtoClient *s);

#endif
