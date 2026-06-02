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
#include <limits.h>
#include <string.h>

static LogProtoStatus log_proto_text_client_flush(LogProtoClient *s);

static gboolean
log_proto_text_client_poll_prepare(LogProtoClient *s, GIOCondition *cond, GIOCondition *idle_cond, gint *timeout)
{
  LogProtoTextClient *self = (LogProtoTextClient *) s;

  *cond = G_IO_OUT | G_IO_IN;
  *idle_cond = G_IO_IN;

  return self->partial != NULL || self->batch.count > 0;
}

static gboolean
log_proto_unidirectional_text_client_poll_prepare(LogProtoClient *s, GIOCondition *cond, GIOCondition *idle_cond,
                                                  gint *timeout)
{
  LogProtoTextClient *self = (LogProtoTextClient *) s;

  *cond = G_IO_OUT;
  *idle_cond = 0;

  return self->partial != NULL || self->batch.count > 0;
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


static LogProtoStatus
_flush_single(LogProtoTextClient *self)
{
  gint rc;

  if (!self->partial)
    {
      return LPS_SUCCESS;
    }

  /* attempt to flush previously buffered data */
  gint len = self->partial_len - self->partial_pos;

  rc = log_transport_stack_write(&self->super.transport_stack, &self->partial[self->partial_pos], len);
  if (rc < 0)
    {
      if (errno != EAGAIN && errno != EINTR)
        {
          msg_error("I/O error occurred while writing",
                    evt_tag_int("fd", self->super.transport_stack.fd),
                    evt_tag_error(EVT_TAG_OSERROR));
          return LPS_ERROR;
        }

      return LPS_SUCCESS;
    }

  if (rc != len)
    {
      self->partial_pos += rc;
      return LPS_PARTIAL;
    }

  if (self->partial_free)
    self->partial_free(self->partial);
  self->partial = NULL;
  if (self->next_state >= 0)
    {
      self->state = self->next_state;
      self->next_state = -1;
    }

  log_proto_client_msg_ack(&self->super, 1);

  /* NOTE: we return here to give a chance to the framed protocol to send the frame header. */
  return LPS_SUCCESS;
}

/* advance over `written` freshly sent bytes, freeing and acking each message
 * that went out whole, then compacting the unsent tail to the front so the
 * freed slots can be reused right away.  only the iovec descriptors move, not
 * the payloads; a short write leaves the head's offset in partial_pos. */
static void
_advance_batch(LogProtoTextClient *self, gsize written)
{
  gint acked = 0;
  while (acked < self->batch.count)
    {
      gsize remaining = self->batch.iov[acked].iov_len - self->batch.partial_pos;
      if (written < remaining)
        {
          self->batch.partial_pos += written;
          break;
        }

      written -= remaining;
      g_free(self->batch.iov[acked].iov_base);
      self->batch.partial_pos = 0;
      ++acked;
    }

  if (acked)
    {
      log_proto_client_msg_ack(&self->super, acked);
      self->batch.count -= acked;
      memmove(self->batch.iov, &self->batch.iov[acked], self->batch.count * sizeof(*self->batch.iov));
    }
}

/* write the queued batch out with a single writev(), resuming from the head's
 * offset when an earlier short write left a tail behind. */
static LogProtoStatus
_flush_batch(LogProtoTextClient *self)
{
  if (self->batch.count == 0)
    return LPS_SUCCESS;

  struct iovec *iov = self->batch.iov;
  gint iov_count = self->batch.count;

  /* the first pending chunk may be partially sent from an earlier short write */
  struct iovec first = iov[0];
  iov[0].iov_base = (guchar *) first.iov_base + self->batch.partial_pos;
  iov[0].iov_len = first.iov_len - self->batch.partial_pos;

  gssize rc = log_transport_stack_writev(&self->super.transport_stack, iov, iov_count);
  iov[0] = first;

  if (rc < 0)
    {
      if (errno == EAGAIN || errno == EINTR)
        return LPS_SUCCESS;
      msg_error("I/O error occurred while writing",
                evt_tag_int("fd", self->super.transport_stack.fd),
                evt_tag_error(EVT_TAG_OSERROR));
      return LPS_ERROR;
    }

  _advance_batch(self, rc);
  return LPS_SUCCESS;
}

static inline gboolean
_batching_enabled(LogProtoTextClient *self)
{
  if (self->batch.size <= 1)
    return FALSE;

  LogTransport *t = log_transport_stack_get_active(&self->super.transport_stack);
  return t && t->writev;
}

static LogProtoStatus
log_proto_text_client_flush(LogProtoClient *s)
{
  LogProtoTextClient *self = (LogProtoTextClient *) s;

  if (!_batching_enabled(self))
    return _flush_single(self);

  return _flush_batch(self);
}

LogProtoStatus
log_proto_text_client_submit_write(LogProtoClient *s, guchar *msg, gsize msg_len, GDestroyNotify msg_free,
                                   gint next_state)
{
  LogProtoTextClient *self = (LogProtoTextClient *) s;

  g_assert(self->partial == NULL);
  self->partial = msg;
  self->partial_len = msg_len;
  self->partial_pos = 0;
  self->partial_free = msg_free;
  self->next_state = next_state;
  return log_proto_text_client_flush(s);
}


static LogProtoStatus
_post_single(LogProtoClient *s, guchar *msg, gsize msg_len, gboolean *consumed)
{
  LogProtoTextClient *self = (LogProtoTextClient *) s;

  /* try to flush already buffered data */
  *consumed = FALSE;
  const LogProtoStatus status = log_proto_text_client_flush(s);
  if (status == LPS_ERROR)
    {
      /* log_proto_flush() already logs in the case of an error */
      return status;
    }

  if (self->partial || LPS_PARTIAL == status)
    {
      /* NOTE: the partial buffer has not been emptied yet even with the
       * flush above, we shouldn't attempt to write again.
       *
       * Otherwise: with the framed protocol this could case the frame
       * header to be split, and interleaved with message payload, as in:
       *
       *     First bytes of frame header || payload || tail of frame header.
       *
       * This obviously would cause the framing to break. Also libssl
       * returns an error in this case, which is how this was discovered.
       */
      return LPS_PARTIAL;
    }

  *consumed = TRUE;
  return log_proto_text_client_submit_write(s, msg, msg_len, (GDestroyNotify) g_free, -1);
}

static inline gboolean
_can_queue_to_batch(LogProtoTextClient *self)
{
  return self->batch.count < self->batch.size;
}

/*
 * log_proto_text_client_post:
 * @msg: formatted log message to send (this might be consumed by this function)
 * @msg_len: length of @msg
 * @consumed: pointer to a gboolean that gets set if the message was consumed by this function
 * @error: error information, if any
 *
 * This function posts a message to the log transport, performing buffering
 * of partially sent data if needed. The return value indicates whether we
 * successfully sent this message, or if it should be resent by the caller.
 **/
static LogProtoStatus
log_proto_text_client_post(LogProtoClient *s, LogMessage *logmsg, guchar *msg, gsize msg_len, gboolean *consumed)
{
  LogProtoTextClient *self = (LogProtoTextClient *) s;

  if (!_batching_enabled(self))
    return _post_single(s, msg, msg_len, consumed);

  *consumed = FALSE;

  if (!_can_queue_to_batch(self))
    {
      LogProtoStatus result = log_proto_text_client_flush(s);
      if (result != LPS_SUCCESS || !_can_queue_to_batch(self))
        /* no room yet (flush failed or buffer not drained) -> caller resends */
        return result;
    }

  self->batch.iov[self->batch.count].iov_base = (void *) msg;
  self->batch.iov[self->batch.count].iov_len = msg_len;
  ++self->batch.count;
  *consumed = TRUE;

  if (self->batch.count == self->batch.size)
    return log_proto_text_client_flush(s);

  return LPS_SUCCESS;
}

void
log_proto_text_client_free(LogProtoClient *s)
{
  LogProtoTextClient *self = (LogProtoTextClient *)s;
  if (self->partial_free)
    self->partial_free(self->partial);
  self->partial = NULL;
  /* messages still queued in the batch were consumed but never acked, so they
   * are held only in the flow-control backlog; rewind them before we drop the
   * buffers, otherwise a teardown on reconnect would lose them */
  if (self->batch.count > 0)
    log_proto_client_msg_rewind(&self->super);
  for (gint i = 0; i < self->batch.count; ++i)
    g_free(self->batch.iov[i].iov_base);
  g_free(self->batch.iov);
  self->batch.iov = NULL;
  log_proto_client_free_method(s);
};

static gint
_calculate_batch_size(const LogProtoClientOptions *options)
{
  gint batch_size = (options && options->flush_lines > 1) ? options->flush_lines : 1;
#ifdef IOV_MAX
  batch_size = MIN(batch_size, IOV_MAX);
#endif
  return batch_size;
}

void
log_proto_text_client_init(LogProtoTextClient *self, LogTransport *transport, const LogProtoClientOptions *options)
{
  log_proto_client_init(&self->super, transport, options);
  self->super.poll_prepare = log_proto_text_client_poll_prepare;
  self->super.flush = log_proto_text_client_flush;
  self->super.process_in = log_proto_text_client_process_input;
  self->super.post = log_proto_text_client_post;
  self->super.free_fn = log_proto_text_client_free;
  self->next_state = -1;

  self->batch.size = _calculate_batch_size(options);
  self->batch.iov = g_new0(struct iovec, self->batch.size);
}

LogProtoClient *
log_proto_text_client_new(LogTransport *transport, const LogProtoClientOptions *options)
{
  LogProtoTextClient *self = g_new0(LogProtoTextClient, 1);

  log_proto_text_client_init(self, transport, options);
  return &self->super;
}

LogProtoClient *
log_proto_unidirectional_text_client_new(LogTransport *transport, const LogProtoClientOptions *options)
{
  LogProtoTextClient *self = g_new0(LogProtoTextClient, 1);

  log_proto_text_client_init(self, transport, options);
  self->super.poll_prepare = log_proto_unidirectional_text_client_poll_prepare;
  self->super.process_in = _prohibit_in;

  return &self->super;
}
