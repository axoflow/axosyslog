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

#ifndef LOGPROTO_LUMBERJACK_SERVER_H_INCLUDED
#define LOGPROTO_LUMBERJACK_SERVER_H_INCLUDED

#include "lumberjack-proto-options.h"
#include "stats/stats-cluster-key-builder.h"

LogProtoServer *log_proto_lumberjack_server_new(LogTransport *transport, const LogProtoServerOptions *options,
                                                const LumberjackReceiverOptions *lumberjack_options,
                                                StatsClusterKeyBuilder *kb);

/* test only: run the handler of the keepalive timer right now */
void log_proto_lumberjack_server_fire_keepalive(LogProtoServer *s);

#endif
