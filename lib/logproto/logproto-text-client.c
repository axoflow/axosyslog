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
#include "logproto-text-client.h"
#include "messages.h"

#include <errno.h>
#include <string.h>

static gboolean
log_proto_text_client_poll_prepare(LogProtoClient *s, GIOCondition *cond, GIOCondition *idle_cond, gint *timeout)
{
  LogProtoTextClient *self = (LogProtoTextClient *) s;

  *cond = G_IO_OUT | G_IO_IN;
  *idle_cond = G_IO_IN;

  return self->batch.count > 0;
}

static gboolean
log_proto_unidirectional_text_client_poll_prepare(LogProtoClient *s, GIOCondition *cond, GIOCondition *idle_cond,
                                                  gint *timeout)
{
  LogProtoTextClient *self = (LogProtoTextClient *) s;

  *cond = G_IO_OUT;
  *idle_cond = 0;

  return self->batch.count > 0;
}

static LogProtoStatus
log_proto_text_client_process_input(LogProtoClient *s)
{
  LogProtoTextClient *self = (LogProtoTextClient *) s;
  guchar buf[1024];
  gssize rc = -1;
  gsize read_limit = 0;
  gsize read = 0;

  do
    {
      rc = log_transport_stack_read(&self->super.transport_stack, buf, sizeof(buf), NULL);
      read_limit++;

      if (rc > 0)
        read += rc;
    }
  while (rc > 0 && read_limit < 100);

  if (read > 0 && !self->super.options->drop_input)
    {
      msg_error("Unexpected input, closing", evt_tag_int("fd", self->super.transport_stack.fd));
      return LPS_ERROR;
    }

  if (rc < 0)
    {
      if (errno != EAGAIN)
        {
          msg_error("Error reading data", evt_tag_int("fd", self->super.transport_stack.fd), evt_tag_error("error"));
          return LPS_ERROR;
        }

      return LPS_SUCCESS;
    }

  if (rc == 0)
    {
      msg_notice("EOF occurred", evt_tag_int("fd", self->super.transport_stack.fd));
      return LPS_EOF;
    }

  return LPS_SUCCESS;
}

static LogProtoStatus
_prohibit_in(LogProtoClient *s)
{
  g_assert_not_reached();
  return LPS_ERROR;
}

/* keep the buffer of a regular batch, but release the one that a batch of
 * oversized messages left behind */
#define TEXT_CLIENT_BATCH_BUFFER_MAX (1024 * 1024)

static inline gboolean
_batching_enabled(LogProtoTextClient *self)
{
  if (self->batch.size <= 1)
    return FALSE;

  /* datagram transports have no writev(), each message must be written
   * separately to remain a separate datagram */
  LogTransport *t = log_transport_stack_get_active(&self->super.transport_stack);
  return t && t->writev;
}

static inline gint
_batch_limit(LogProtoTextClient *self)
{
  return _batching_enabled(self) ? self->batch.size : 1;
}

static void
_reset_batch(LogProtoTextClient *self)
{
  if (self->batch.data->allocated_len > TEXT_CLIENT_BATCH_BUFFER_MAX)
    {
      g_string_free(self->batch.data, TRUE);
      self->batch.data = g_string_sized_new(0);
    }
  else
    {
      g_string_truncate(self->batch.data, 0);
    }
  self->batch.written = 0;
  self->batch.count = 0;
  self->batch.acked = 0;
  self->batch.acked_len = 0;
  self->batch.draining = FALSE;
}

/* ack the messages that have been written completely since the last call */
static void
_ack_written(LogProtoTextClient *self)
{
  gint num_acked = 0;

  while (self->batch.acked < self->batch.count &&
         self->batch.acked_len + self->batch.msg_lens[self->batch.acked] <= self->batch.written)
    {
      self->batch.acked_len += self->batch.msg_lens[self->batch.acked];
      self->batch.acked++;
      num_acked++;
    }

  if (num_acked)
    log_proto_client_msg_ack(&self->super, num_acked);
}

static LogProtoStatus
log_proto_text_client_flush(LogProtoClient *s)
{
  LogProtoTextClient *self = (LogProtoTextClient *) s;

  if (self->batch.count == 0)
    return LPS_SUCCESS;

  gsize len = self->batch.data->len - self->batch.written;
  if (len > 0)
    {
      gssize rc = log_transport_stack_write(&self->super.transport_stack,
                                            self->batch.data->str + self->batch.written, len);
      if (rc < 0)
        {
          if (errno != EAGAIN && errno != EINTR)
            {
              msg_error("I/O error occurred while writing",
                        evt_tag_int("fd", self->super.transport_stack.fd),
                        evt_tag_error(EVT_TAG_OSERROR));
              return LPS_ERROR;
            }
          self->batch.draining = TRUE;
          return LPS_SUCCESS;
        }
      self->batch.written += rc;
    }

  _ack_written(self);
  if (self->batch.written == self->batch.data->len)
    _reset_batch(self);
  else
    self->batch.draining = TRUE;
  return LPS_SUCCESS;
}

/*
 * Append a message, preceded by @prefix (e.g. a frame header), to the
 * batch.  The message is copied, *consumed is set if it was accepted,
 * otherwise the caller has to post it again once the pending data is
 * written.
 */
LogProtoStatus
log_proto_text_client_post_with_prefix(LogProtoClient *s, const guchar *prefix, gsize prefix_len,
                                       const guchar *msg, gsize msg_len, gboolean *consumed)
{
  LogProtoTextClient *self = (LogProtoTextClient *) s;
  gint limit = _batch_limit(self);

  *consumed = FALSE;

  if (self->batch.draining || self->batch.count >= limit)
    {
      LogProtoStatus status = log_proto_text_client_flush(s);
      if (status != LPS_SUCCESS || self->batch.draining || self->batch.count >= limit)
        return status;
    }

  g_string_append_len(self->batch.data, (const gchar *) prefix, prefix_len);
  g_string_append_len(self->batch.data, (const gchar *) msg, msg_len);
  self->batch.msg_lens[self->batch.count++] = prefix_len + msg_len;
  *consumed = TRUE;

  if (self->batch.count >= limit)
    return log_proto_text_client_flush(s);

  return LPS_SUCCESS;
}

LogProtoStatus
log_proto_text_client_post_method(LogProtoClient *s, LogMessage *logmsg, const guchar *msg, gsize msg_len,
                                  gboolean *consumed)
{
  return log_proto_text_client_post_with_prefix(s, NULL, 0, msg, msg_len, consumed);
}

void
log_proto_text_client_free_method(LogProtoClient *s)
{
  LogProtoTextClient *self = (LogProtoTextClient *)s;

  /* messages still queued in the batch were consumed but never acked, so they
   * are held only in the flow-control backlog; rewind them before we drop the
   * buffer, otherwise a teardown on reconnect would lose them */
  if (self->batch.count > self->batch.acked)
    log_proto_client_msg_rewind(&self->super);
  g_string_free(self->batch.data, TRUE);
  g_free(self->batch.msg_lens);
  log_proto_client_free_method(s);
};

void
log_proto_text_client_init(LogProtoTextClient *self, LogTransport *transport,
                           const LogProtoClientOptions *options, gboolean batching_supported)
{
  log_proto_client_init(&self->super, transport, options);
  self->super.poll_prepare = log_proto_text_client_poll_prepare;
  self->super.flush = log_proto_text_client_flush;
  self->super.process_in = log_proto_text_client_process_input;
  self->super.post = log_proto_text_client_post_method;
  self->super.free_fn = log_proto_text_client_free_method;

  self->batch.size = 1;
  if (batching_supported && options && options->flush_lines > 1)
    self->batch.size = options->flush_lines;
  self->batch.data = g_string_sized_new(0);
  self->batch.msg_lens = g_new0(gsize, self->batch.size);
}

LogProtoClient *
log_proto_text_client_new(LogTransport *transport, const LogProtoClientOptions *options)
{
  LogProtoTextClient *self = g_new0(LogProtoTextClient, 1);

  log_proto_text_client_init(self, transport, options, TRUE);
  return &self->super;
}

LogProtoClient *
log_proto_unidirectional_text_client_new(LogTransport *transport, const LogProtoClientOptions *options)
{
  LogProtoTextClient *self = g_new0(LogProtoTextClient, 1);

  log_proto_text_client_init(self, transport, options, TRUE);
  self->super.poll_prepare = log_proto_unidirectional_text_client_poll_prepare;
  self->super.process_in = _prohibit_in;

  return &self->super;
}
