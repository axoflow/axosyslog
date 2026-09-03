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

#ifndef ALTP_PROTO_OPTIONS_H_INCLUDED
#define ALTP_PROTO_OPTIONS_H_INCLUDED

#include "altp-session.h"
#include "logproto/logproto-server.h"

/* the default TCP port of an ALTP Receiver (specification 4.1) */
#define ALTP_DEFAULT_PORT 35514

/* Receiver side defaults of specification 11, Appendix C */
#define ALTP_DEFAULT_ACK_TIMEOUT 900
#define ALTP_DEFAULT_SESSION_EXPIRATION 2592000
#define ALTP_DEFAULT_IDLE_TIMEOUT 60

/* The TLS policy of the Receiver (6.1).  AUTO, the default, resolves to
 * REQUIRED when the driver configured tls() and to NONE otherwise (ADR-0008).
 */
typedef enum
{
  ALTP_TLS_POLICY_AUTO,
  ALTP_TLS_POLICY_NONE,
  ALTP_TLS_POLICY_OPTIONAL,
  ALTP_TLS_POLICY_REQUIRED,
} AltpTlsPolicy;

/* The ALTP specific settings of one Receiver, in a struct of their own so that
 * a LogProtoServer can be handed a copy without casting its
 * LogProtoServerOptions to the extended type below.
 */
typedef struct _AltpReceiverOptions
{
  /* seconds to wait for durability before acknowledging partially (11) */
  gint ack_timeout;
  /* seconds after which an unused Session Record is expired */
  gint session_expiration;
  AltpTlsPolicy tls_policy;
} AltpReceiverOptions;

/* The per Receiver state shared by every Connection of one driver: it owns the
 * LogProtoServerFactory the grammar hands to afsocket and a reference of the
 * Session Registry the Connections look their Sessions up in.
 *
 * Stage B2: this is also where the PersistState of the configuration and the
 * periodic Session Record expiry timer belong.
 */
typedef struct _AltpReceiverContext AltpReceiverContext;

/* Bind the receiver context to the persistent state and to the persistent name
 * of its driver, which is what scopes the Session Registry (ADR-0005).  Every
 * Connection of the driver calls this right after it was constructed, and only
 * the first call has an effect.
 */
void altp_receiver_context_bind_persist_state(AltpReceiverContext *self, PersistState *state,
                                              const gchar *persist_name);

/* The Session Registry of this Receiver.  Until a Connection delivered the
 * persistent name of the driver -- which never happens in a unit test without
 * one -- the registry is keyed by a name of the context itself.
 */
AltpSessionRegistry *altp_receiver_context_get_registry(AltpReceiverContext *self);

/* AltpProtoServerOptions extends LogProtoServerOptions in place, living inside
 * the LogProtoServerOptionsStorage union of the driver's LogReaderOptions,
 * exactly like LogProtoFileReaderOptions does (see the long warning comment in
 * modules/affile/logproto-file-reader.c).  Only the fields we add are ours:
 * the embedded super belongs to the LogReader.
 */
typedef struct _AltpProtoServerOptions
{
  LogProtoServerOptions super;
  AltpReceiverOptions altp;
  AltpReceiverContext *context;
} AltpProtoServerOptions;

G_STATIC_ASSERT(sizeof(AltpProtoServerOptions) <= LOG_PROTO_SERVER_OPTIONS_SIZE);

/* Set the ALTP specific part of a LogProtoServerOptionsStorage up: the
 * defaults, the receiver context with its factory and the destroy hook.  @s
 * must point to a storage union and not to a bare LogProtoServerOptions.
 */
AltpProtoServerOptions *altp_proto_server_options_defaults(LogProtoServerOptions *s);

LogProtoServerFactory *altp_proto_server_options_get_factory(AltpProtoServerOptions *self);

#endif
