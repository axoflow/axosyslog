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

#include "lumberjack-proto-options.h"
#include "logproto-lumberjack-server.h"
#include "ack-tracker/ack_tracker_factory.h"

static LogProtoServer *
_construct_proto(LogTransport *transport, const LogProtoServerOptions *o, StatsClusterKeyBuilder *kb)
{
  const LumberjackProtoServerOptions *options = (const LumberjackProtoServerOptions *) o;

  return log_proto_lumberjack_server_new(transport, o, &options->lumberjack, kb);
}

static LogProtoServerFactory lumberjack_proto_server_factory =
{
  .construct = _construct_proto,
  .default_inet_port = LUMBERJACK_DEFAULT_PORT,
  .stateful = FALSE,
};

LumberjackProtoServerOptions *
lumberjack_proto_server_options_defaults(LogProtoServerOptions *o)
{
  LumberjackProtoServerOptions *options = (LumberjackProtoServerOptions *) o;

  options->lumberjack.max_window_size = LUMBERJACK_DEFAULT_MAX_WINDOW_SIZE;
  options->lumberjack.keepalive_interval = LUMBERJACK_DEFAULT_KEEPALIVE_INTERVAL;
  options->lumberjack.window_timeout = LUMBERJACK_DEFAULT_WINDOW_TIMEOUT;

  /* an ACK covers every frame of a window (8.1), which is a prefix of the
   * frames delivered on the connection: the consecutive ack tracker reports
   * durability as exactly such a prefix */
  log_proto_server_options_set_ack_tracker_factory(&options->super, consecutive_ack_tracker_factory_new());

  return options;
}

LogProtoServerFactory *
lumberjack_proto_server_options_get_factory(LumberjackProtoServerOptions *options)
{
  return &lumberjack_proto_server_factory;
}
