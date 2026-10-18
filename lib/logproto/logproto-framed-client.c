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
#include "logproto-framed-client.h"
#include "logproto-text-client.h"
#include "messages.h"

/* the longest frame header is "9999999 " */
#define FRAME_HEADER_MAX 9

static LogProtoStatus
log_proto_framed_client_post(LogProtoClient *s, LogMessage *logmsg, const guchar *msg, gsize msg_len,
                             gboolean *consumed)
{
  guchar frame_hdr[FRAME_HEADER_MAX];
  gsize orig_msg_len = msg_len;

  if (msg_len > 9999999)
    msg_len = 9999999;

  gint frame_hdr_len = g_snprintf((gchar *) frame_hdr, sizeof(frame_hdr), "%" G_GSIZE_FORMAT " ", msg_len);
  LogProtoStatus status = log_proto_text_client_post_with_prefix(s, frame_hdr, frame_hdr_len, msg, msg_len, consumed);

  /* only once the message is accepted, so a retried message warns once */
  if (*consumed && orig_msg_len != msg_len)
    msg_warning("Error, message length too large for framed protocol, truncated",
                evt_tag_int("length", orig_msg_len));
  return status;
}

LogProtoClient *
log_proto_framed_client_new(LogTransport *transport, const LogProtoClientOptions *options)
{
  LogProtoTextClient *self = g_new0(LogProtoTextClient, 1);

  log_proto_text_client_init(self, transport, options, TRUE);
  self->super.post = log_proto_framed_client_post;

  return &self->super;
}
