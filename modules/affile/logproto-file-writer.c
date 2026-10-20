/*
 * Copyright (c) 2002-2012 Balabit
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 1998-2012 Balázs Scheidler
 * Copyright (c) 2024 László Várady
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

#include "logproto-file-writer.h"
#include "messages.h"

#include <string.h>
#include <errno.h>
#include <unistd.h>

typedef struct _LogProtoFileWriter
{
  LogProtoClient super;
  gboolean fsync;

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
  } batch;
} LogProtoFileWriter;

/* keep the buffer of a regular batch, but release the one that a batch of
 * oversized messages left behind */
#define FILE_WRITER_BATCH_BUFFER_MAX (1024 * 1024)

static void
_reset_batch(LogProtoFileWriter *self)
{
  if (self->batch.data->allocated_len > FILE_WRITER_BATCH_BUFFER_MAX)
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
}

/* ack the messages that have been written completely since the last call */
static void
_ack_written(LogProtoFileWriter *self)
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

/*
 * log_proto_file_writer_flush:
 *
 * this function flushes the file output buffer
 * it is called either form log_proto_file_writer_post (normal mode: the buffer is full)
 * or from log_proto_flush (foced flush: flush time, exit, etc)
 *
 */
static LogProtoStatus
log_proto_file_writer_flush(LogProtoClient *s)
{
  LogProtoFileWriter *self = (LogProtoFileWriter *)s;

  /* we might be called from log_writer_deinit() without having a buffer at all */
  if (self->batch.count == 0)
    return LPS_SUCCESS;

  gsize len = self->batch.data->len - self->batch.written;
  if (len > 0)
    {
      gssize rc = log_transport_stack_write(&self->super.transport_stack,
                                            self->batch.data->str + self->batch.written, len);

      if (rc > 0 && self->fsync)
        fsync(self->super.transport_stack.fd);

      if (rc < 0)
        {
          if (errno == EINTR || errno == EAGAIN)
            return LPS_SUCCESS;

          /* the rewound messages are posted again, don't keep them */
          log_proto_client_msg_rewind(&self->super);
          _reset_batch(self);
          msg_error("I/O error occurred while writing",
                    evt_tag_int("fd", self->super.transport_stack.fd),
                    evt_tag_error(EVT_TAG_OSERROR));
          return LPS_ERROR;
        }
      self->batch.written += rc;
    }

  _ack_written(self);
  if (self->batch.written < self->batch.data->len)
    return LPS_PARTIAL;

  _reset_batch(self);
  return LPS_SUCCESS;
}

/*
 * log_proto_file_writer_post:
 * @msg: formatted log message to send, copied if it is consumed
 * @msg_len: length of @msg
 * @consumed: pointer to a gboolean that gets set if the message was consumed by this function
 * @error: error information, if any
 *
 * This function posts a message to the log transport, performing buffering
 * of partially sent data if needed. The return value indicates whether we
 * successfully sent this message, or if it should be resent by the caller.
 **/
static LogProtoStatus
log_proto_file_writer_post(LogProtoClient *s, LogMessage *logmsg, const guchar *msg, gsize msg_len,
                           gboolean *consumed)
{
  LogProtoFileWriter *self = (LogProtoFileWriter *)s;
  LogProtoStatus result;

  *consumed = FALSE;
  if (self->batch.count >= self->batch.size || self->batch.written > 0)
    {
      result = log_proto_file_writer_flush(s);
      if (result != LPS_SUCCESS || self->batch.count >= self->batch.size || self->batch.written > 0)
        {
          /* don't consume a new message if flush failed OR if we couldn't
           * progress in flush() to empty the outgoing buffer (either
           * full or partially written)
           */
          return result;
        }
    }

  /* register the new message */
  g_string_append_len(self->batch.data, (const gchar *) msg, msg_len);
  self->batch.msg_lens[self->batch.count++] = msg_len;

  *consumed = TRUE;

  if (self->batch.count == self->batch.size)
    {
      /* we have reached the max buffer size -> we need to write the messages */
      return log_proto_file_writer_flush(s);
    }

  return LPS_SUCCESS;
}

static gboolean
log_proto_file_writer_poll_prepare(LogProtoClient *s, GIOCondition *cond, GIOCondition *idle_cond, gint *timeout)
{
  LogProtoFileWriter *self = (LogProtoFileWriter *) s;

  if (log_transport_stack_poll_prepare(&self->super.transport_stack, cond))
    return TRUE;

  /* if there's no pending I/O in the transport layer, then we want to do a write */
  if (*cond == 0)
    *cond = G_IO_OUT;
  return self->batch.count > 0;
}

static void
log_proto_file_writer_free(LogProtoClient *s)
{
  LogProtoFileWriter *self = (LogProtoFileWriter *) s;

  /* the messages that were not written completely are only held by the
   * flow-control backlog, rewind them so they are not lost */
  if (self->batch.count > self->batch.acked)
    log_proto_client_msg_rewind(&self->super);
  g_string_free(self->batch.data, TRUE);
  g_free(self->batch.msg_lens);
  log_proto_client_free_method(s);
}

LogProtoClient *
log_proto_file_writer_new(LogTransport *transport, const LogProtoClientOptions *options, gint flush_lines, gint fsync_)
{
  if (flush_lines == 0)
    /* the flush-lines option has not been specified, use a default value */
    flush_lines = 1;

  LogProtoFileWriter *self = g_new0(LogProtoFileWriter, 1);

  log_proto_client_init(&self->super, transport, options);
  self->batch.size = flush_lines;
  self->batch.data = g_string_sized_new(0);
  self->batch.msg_lens = g_new0(gsize, flush_lines);
  self->fsync = fsync_;
  self->super.poll_prepare = log_proto_file_writer_poll_prepare;
  self->super.post = log_proto_file_writer_post;
  self->super.flush = log_proto_file_writer_flush;
  self->super.free_fn = log_proto_file_writer_free;
  return &self->super;
}
