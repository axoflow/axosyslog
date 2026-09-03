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

#ifndef LOGPROTO_ALTP_SERVER_H_INCLUDED
#define LOGPROTO_ALTP_SERVER_H_INCLUDED

#include "altp-proto-options.h"
#include "stats/stats-cluster-key-builder.h"

/* a command line is at most 512 octets, terminator included (4.2, 16 A) */
#define ALTP_MAX_COMMAND_LINE 512

/* The Receiver side of one ALTP Connection.  @altp_options is not copied by
 * reference: the proto takes a copy, so that it survives a configuration
 * reload that recreates the driver while keeping this Connection alive.
 */
LogProtoServer *log_proto_altp_server_new(LogTransport *transport, const LogProtoServerOptions *options,
                                          const AltpReceiverOptions *altp_options, StatsClusterKeyBuilder *kb);

#endif
