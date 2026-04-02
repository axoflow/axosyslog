/*
 * Copyright (c) 2024 Balázs Scheidler <balazs.scheidler@axoflow.com>
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
#include "logproto-auto-server.h"
#include "logproto-text-server.h"
#include "logproto-framed-server.h"
#include "logproto.h"
#include "transport/logtransport.h"
#include "messages.h"
#include "str-utils.h"

G_STATIC_ASSERT(LOG_TRANSPORT_RA_BUFFER_SIZE >= RFC6587_MAX_FRAME_LEN_DIGITS + 1);

typedef struct _LogProtoAutoServer
{
  LogProtoServer super;
} LogProtoAutoServer;

static inline gboolean
_is_frame_octet_count(const gchar *detect_buffer, gsize detect_buffer_len, gboolean *need_more_data)
{
  if (!ch_isdigit(detect_buffer[0]))
    return FALSE;

  for (gsize i = 1; i < detect_buffer_len; i++)
    {
      if (ch_isdigit(detect_buffer[i]) && i < RFC6587_MAX_FRAME_LEN_DIGITS)
        continue;

      if (detect_buffer[i] == ' ')
        return TRUE;

      return FALSE;
    }

  *need_more_data = TRUE;
  return TRUE;
}

static LogProtoServer *
_construct_detected_proto(LogProtoAutoServer *self, const gchar *detect_buffer, gsize detect_buffer_len)
{
  gint fd = self->super.transport_stack.fd;

  gboolean need_more_data = FALSE;
  if (_is_frame_octet_count(detect_buffer, detect_buffer_len, &need_more_data))
    {
      if (need_more_data)
        return NULL;

      msg_debug("Auto-detected octet-counted-framing, using framed protocol",
                evt_tag_int("fd", fd));
      return log_proto_framed_server_new(NULL, self->super.options);
    }

  if (detect_buffer[0] == '<')
    {
      msg_debug("Auto-detected non-transparent-framing, using simple text protocol",
                evt_tag_int("fd", fd));
    }
  else
    {
      msg_debug("Unable to detect framing, falling back to simple text transport",
                evt_tag_int("fd", fd),
                evt_tag_mem("detect_buffer", detect_buffer, detect_buffer_len));
    }
  return log_proto_text_server_new(NULL, self->super.options);
}

static LogProtoPrepareAction
log_proto_auto_server_poll_prepare(LogProtoServer *s, GIOCondition *cond, gint *timeout G_GNUC_UNUSED)
{
  LogProtoAutoServer *self = (LogProtoAutoServer *) s;

  /* LPPA_FORCE_SCHEDULE_FETCH based on log_transport_stack_poll_prepare()
   * should NOT be requested here as read_ahead() is used */
  log_transport_stack_poll_prepare(&self->super.transport_stack, cond);

  if (*cond == 0)
    *cond = G_IO_IN;

  return LPPA_POLL_IO;
}

static LogProtoStatus
log_proto_auto_handshake(LogProtoServer *s, gboolean *handshake_finished, LogProtoServer **proto_replacement)
{
  LogProtoAutoServer *self = (LogProtoAutoServer *) s;

  gint rc;
  const gchar *detect_buffer = log_transport_stack_look_ahead(&self->super.transport_stack, &rc);

  if (rc == 0)
    return LPS_EOF;
  else if (rc < 0)
    {
      if (errno == EAGAIN)
        return LPS_AGAIN;
      return LPS_ERROR;
    }

  *proto_replacement = _construct_detected_proto(self, detect_buffer, rc);
  if (!*proto_replacement)
    return LPS_AGAIN;

  return LPS_SUCCESS;
}

LogProtoServer *
log_proto_auto_server_new(LogTransport *transport, const LogProtoServerOptions *options)
{
  LogProtoAutoServer *self = g_new0(LogProtoAutoServer, 1);

  /* we are not using our own transport stack, transport is to be passed to
   * the LogProto implementation once we finished with detection */

  log_proto_server_init(&self->super, transport, options);
  self->super.handshake = log_proto_auto_handshake;
  self->super.poll_prepare = log_proto_auto_server_poll_prepare;
  return &self->super;
}
