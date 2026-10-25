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
#include "logproto/logproto-text-client.h"

#include <unistd.h>

typedef struct _LogProtoFileWriter
{
  LogProtoTextClient super;
  gboolean fsync;
} LogProtoFileWriter;

static LogProtoStatus
log_proto_file_writer_flush(LogProtoClient *s)
{
  LogProtoFileWriter *self = (LogProtoFileWriter *) s;
  gsize pending = self->super.batch.data->len - self->super.batch.written;

  LogProtoStatus status = log_proto_text_client_flush_method(s);
  if (status == LPS_SUCCESS && self->fsync &&
      self->super.batch.data->len - self->super.batch.written < pending)
    fsync(self->super.super.transport_stack.fd);

  return status;
}

static gboolean
log_proto_file_writer_poll_prepare(LogProtoClient *s, GIOCondition *cond, GIOCondition *idle_cond, gint *timeout)
{
  LogProtoFileWriter *self = (LogProtoFileWriter *) s;

  if (log_transport_stack_poll_prepare(&self->super.super.transport_stack, cond))
    return TRUE;

  /* if there's no pending I/O in the transport layer, then we want to do a write */
  if (*cond == 0)
    *cond = G_IO_OUT;
  return self->super.batch.count > 0;
}

LogProtoClient *
log_proto_file_writer_new(LogTransport *transport, const LogProtoClientOptions *options, gint flush_lines, gint fsync_)
{
  LogProtoFileWriter *self = g_new0(LogProtoFileWriter, 1);

  log_proto_text_client_init(&self->super, transport, options, TRUE);
  log_proto_text_client_set_batch_size(&self->super, flush_lines);
  self->fsync = fsync_;
  self->super.super.poll_prepare = log_proto_file_writer_poll_prepare;
  self->super.super.flush = log_proto_file_writer_flush;
  self->super.super.process_in = NULL;
  return &self->super.super;
}
