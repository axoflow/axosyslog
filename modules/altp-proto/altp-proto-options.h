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
#include "logproto/logproto-client.h"
#include "logproto/logproto-server.h"

/* the default TCP port of an ALTP Receiver (specification 4.1) */
#define ALTP_DEFAULT_PORT 35514

/* Defaults of specification 11, Appendix C.  The acknowledgement timeout is
 * the same 900 seconds on both sides, but a different timeout: the Receiver
 * waits for its own pipeline, the Sender for `250 Received n`.
 */
#define ALTP_DEFAULT_ACK_TIMEOUT 900
#define ALTP_DEFAULT_SESSION_EXPIRATION 2592000
#define ALTP_DEFAULT_IDLE_TIMEOUT 60

/* The largest number of Session Records a Receiver keeps.  A Session ID is the
 * only credential of the protocol and the Sender picks it, so without a bound
 * any peer that reaches the port could grow the Session Registry -- and the
 * persistent state behind it -- without end (15).  Local policy, not a wire rule.
 */
#define ALTP_DEFAULT_MAX_SESSIONS 10000


/* What the Receiver does when its acknowledgement timeout expires while it
 * waits for durability (11, 12.1).  CLOSE, the default, abandons the Batch and
 * PARTIAL_ACK acknowledges the durable prefix; see _abandon_batch() in
 * logproto-altp-server.c for why the default is the one it is.
 */
typedef enum
{
  ALTP_ACK_TIMEOUT_ACTION_CLOSE,
  ALTP_ACK_TIMEOUT_ACTION_PARTIAL_ACK,
} AltpAckTimeoutAction;

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
  /* seconds to wait for durability before the Batch is given up on (11) */
  gint ack_timeout;
  AltpAckTimeoutAction ack_timeout_action;
  /* seconds after which an unused Session Record is expired */
  gint session_expiration;
  /* the largest number of Session Records of the Receiver, 0 for unlimited */
  gint max_sessions;
  AltpTlsPolicy tls_policy;
  /* Whether the ZLIB Capability is offered (6.2).  Compression costs CPU on a
   * Receiver that has no say in how much of it a Sender asks for, so it is off
   * unless the user turns it on. */
  gboolean allow_compression;
} AltpReceiverOptions;

/* The per Receiver state shared by every Connection of one driver: it owns the
 * LogProtoServerFactory the grammar hands to afsocket and a reference of the
 * Session Registry the Connections look their Sessions up in.
 */
typedef struct _AltpReceiverContext AltpReceiverContext;

/* Bind the receiver context to the persistent state and to the persistent name
 * of its driver, which is what scopes the Session Registry (ADR-0005).  Every
 * Connection calls this right after it was constructed and only the first call
 * has an effect.
 */
void altp_receiver_context_bind_persist_state(AltpReceiverContext *self, PersistState *state,
                                              const gchar *persist_name, gint session_expiration,
                                              gint max_sessions);

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

/* The largest Frame payload this Sender writes (8.2); a message above it is
 * dropped, see _drop_oversized_message() in logproto-altp-client.c.
 */
#define ALTP_SENDER_DEFAULT_MAX_FRAME_SIZE 65536

/* The Frame count a Sender SHOULD bound a Batch by (8.4), and the ceiling of
 * the bound derived from flush-lines(): the Batch is the unit of
 * acknowledgement, so one that grows without end never becomes durable.
 */
#define ALTP_SENDER_MAX_BATCH_FRAMES 1000

/* The deflate level of the direction a Sender writes (6.2): a local choice,
 * never negotiated, and 6 is what zlib calls the default trade.
 */
#define ALTP_SENDER_DEFAULT_COMPRESSION_LEVEL 6
#define ALTP_SENDER_MAX_COMPRESSION_LEVEL 9

/* The ALTP specific settings of one Sender.  The proto keeps a copy, as the
 * options belong to the driver, which a configuration reload recreates under a
 * Connection kept alive across it.
 */
typedef struct _AltpSenderOptions
{
  /* seconds to wait for `250 Received n` before the Connection is closed and
   * everything unacknowledged retained (9.5) */
  gint ack_timeout;
  /* the largest Frame payload written, in octets (8.2) */
  gint max_frame_size;
  /* Whether ZLIB is requested when the Receiver advertises it (6.2).  A Sender
   * MUST NOT request a Capability that was not advertised, so this only asks. */
  gboolean compression;
  /* the deflate level of the Frames we write, 0 to 9 */
  gint compression_level;
} AltpSenderOptions;

/* AltpProtoClientOptions extends LogProtoClientOptions in place, exactly as
 * AltpProtoServerOptions extends the server side.  Unlike that one it owns no
 * resources: there is no destroy hook on the client options and
 * log_proto_client_options_defaults() is never called by anyone.
 */
typedef struct _AltpProtoClientOptions
{
  LogProtoClientOptions super;
  AltpSenderOptions altp;
} AltpProtoClientOptions;

G_STATIC_ASSERT(sizeof(AltpProtoClientOptions) <= LOG_PROTO_CLIENT_OPTIONS_SIZE);

/* @s must point to a storage union and not to a bare LogProtoClientOptions. */
AltpProtoClientOptions *altp_proto_client_options_defaults(LogProtoClientOptions *s);

LogProtoClientFactory *altp_proto_client_options_get_factory(AltpProtoClientOptions *self);

#endif
