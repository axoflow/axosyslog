/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#include "logproto-lumberjack-server.h"
#include "messages.h"

typedef struct _LogProtoLumberjackServer
{
  LogProtoServer super;
  LumberjackReceiverOptions options;
} LogProtoLumberjackServer;

static LogProtoStatus
log_proto_lumberjack_server_fetch_structured(LogProtoServer *s, LogMessage **msg, LogTransportAuxData *aux,
                                             Bookmark *bookmark)
{
  *msg = NULL;
  return LPS_EOF;
}

static LogProtoPrepareAction
log_proto_lumberjack_server_poll_prepare(LogProtoServer *s, GIOCondition *cond, gint *timeout)
{
  *cond = G_IO_IN;
  *timeout = -1;
  return LPPA_POLL_IO;
}

void
log_proto_lumberjack_server_fire_keepalive(LogProtoServer *s)
{
}

LogProtoServer *
log_proto_lumberjack_server_new(LogTransport *transport, const LogProtoServerOptions *options,
                                const LumberjackReceiverOptions *lumberjack_options, StatsClusterKeyBuilder *kb)
{
  LogProtoLumberjackServer *self = g_new0(LogProtoLumberjackServer, 1);

  log_proto_server_init(&self->super, transport, options);
  self->super.poll_prepare = log_proto_lumberjack_server_poll_prepare;
  self->super.fetch_structured = log_proto_lumberjack_server_fetch_structured;
  self->options = *lumberjack_options;

  return &self->super;
}
