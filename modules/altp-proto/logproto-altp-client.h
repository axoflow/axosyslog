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

#ifndef LOGPROTO_ALTP_CLIENT_H_INCLUDED
#define LOGPROTO_ALTP_CLIENT_H_INCLUDED

#include "altp-proto-options.h"

/* a reply line is at most 512 octets, terminator included (4.3, 16 A) */
#define ALTP_MAX_REPLY_LINE 512

/* the layout version of the "<driver>.altp.sender" entry (10.2) */
#define ALTP_SENDER_PERSIST_VERSION 1

/* The Sender side of one ALTP Connection.  @altp_options is copied, as it
 * belongs to the driver, which a configuration reload recreates while this
 * Connection lives on.
 */
LogProtoClient *log_proto_altp_client_new(LogTransport *transport, const LogProtoClientOptions *options,
                                          const AltpSenderOptions *altp_options);

/* test only: read the persisted Sender state of @persist_name as it stands
 * right now.  FALSE when there is no entry of ours under that name.
 */
gboolean log_proto_altp_client_load_persisted_state(PersistState *state, const gchar *persist_name,
                                                    gchar **session_id, guint32 *frames_sent);

#endif
