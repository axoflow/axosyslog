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

#ifndef LUMBERJACK_PROTO_OPTIONS_H_INCLUDED
#define LUMBERJACK_PROTO_OPTIONS_H_INCLUDED

#include "logproto/logproto-server.h"

/* the port Beats and Logstash use by convention (specification 1.1) */
#define LUMBERJACK_DEFAULT_PORT 5044

/* Defaults of the specification: the canonical receiver bounds the window
 * size at 10 000 (16), Beats send a keepalive every 5 s and the canonical
 * receiver gives a window body 30 s to arrive (10, 13).
 */
#define LUMBERJACK_DEFAULT_MAX_WINDOW_SIZE 10000
#define LUMBERJACK_DEFAULT_KEEPALIVE_INTERVAL 5
#define LUMBERJACK_DEFAULT_WINDOW_TIMEOUT 30

typedef struct _LumberjackReceiverOptions
{
  /* the largest window size accepted, 0 for unlimited (16) */
  gint max_window_size;
  /* seconds between A(0) keepalives while a window waits for its ACK, 0 disables (10) */
  gint keepalive_interval;
  /* seconds a window body may stall before the connection is closed, 0 disables (13) */
  gint window_timeout;
} LumberjackReceiverOptions;

typedef struct _LumberjackProtoServerOptions
{
  LogProtoServerOptions super;
  LumberjackReceiverOptions lumberjack;
} LumberjackProtoServerOptions;

G_STATIC_ASSERT(sizeof(LumberjackProtoServerOptions) <= LOG_PROTO_SERVER_OPTIONS_SIZE);

LumberjackProtoServerOptions *lumberjack_proto_server_options_defaults(LogProtoServerOptions *s);

/* the one factory afsocket constructs every connection of transport(lumberjack) with */
LogProtoServerFactory *lumberjack_proto_server_options_get_factory(LumberjackProtoServerOptions *self);

#endif
