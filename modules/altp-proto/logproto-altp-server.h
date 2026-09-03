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

/* The Receiver side of one ALTP Connection.  Neither @altp_options nor
 * @context is kept by reference: both belong to the driver, which a
 * configuration reload recreates while this Connection lives on.
 */
LogProtoServer *log_proto_altp_server_new(LogTransport *transport, const LogProtoServerOptions *options,
                                          const AltpReceiverOptions *altp_options, AltpReceiverContext *context,
                                          StatsClusterKeyBuilder *kb);

/* test only: run the handler of the acknowledgement timeout right now */
void log_proto_altp_server_fire_ack_timeout(LogProtoServer *s);

#endif
