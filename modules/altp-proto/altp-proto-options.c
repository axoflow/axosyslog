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

#include "altp-proto-options.h"
#include "altp-session.h"
#include "logproto-altp-client.h"
#include "logproto-altp-server.h"
#include "ack-tracker/ack_tracker_factory.h"

struct _AltpReceiverContext
{
  /* one factory per transport(altp(...)) occurrence, i.e. one per driver */
  LogProtoServerFactory factory;

  /* the Session Registry of the Receiver, one reference held; it is taken on
   * demand, as its name -- the persistent name of the driver (ADR-0005) --
   * only arrives once the first Connection is set up */
  AltpSessionRegistry *registry;
  gchar *persist_name;
};

/* The factory of a receiver context is only ever handed out for a
 * LogProtoServerOptionsStorage the grammar initialized, so the cast is safe. */
static LogProtoServer *
_construct_proto(LogTransport *transport, const LogProtoServerOptions *options, StatsClusterKeyBuilder *kb)
{
  const AltpProtoServerOptions *self = (const AltpProtoServerOptions *) options;

  return log_proto_altp_server_new(transport, options, &self->altp, self->context, kb);
}

void
altp_receiver_context_bind_persist_state(AltpReceiverContext *self, PersistState *state, const gchar *persist_name,
                                         gint session_expiration, gint max_sessions)
{
  if (self->persist_name)
    return;

  self->persist_name = g_strdup(persist_name);
  self->registry = altp_session_registry_ref_by_name(self->persist_name);

  /* the registry is module global and keyed by the persistent name, so the
   * driver of a reloaded configuration finds the very same one, and binding is
   * a no-op from the second Receiver on */
  altp_session_registry_bind_persist_state(self->registry, state, session_expiration, max_sessions);
}

AltpSessionRegistry *
altp_receiver_context_get_registry(AltpReceiverContext *self)
{
  if (G_UNLIKELY(!self->registry))
    {
      /* only reachable in a unit test constructing protos without a driver:
       * the Connections of this context still share a registry of their own */
      gchar *name = g_strdup_printf("altp.unnamed(%p)", self);

      self->registry = altp_session_registry_ref_by_name(name);
      g_free(name);
    }

  return self->registry;
}

static AltpReceiverContext *
_altp_receiver_context_new(void)
{
  AltpReceiverContext *self = g_new0(AltpReceiverContext, 1);

  self->factory.construct = _construct_proto;
  self->factory.default_inet_port = ALTP_DEFAULT_PORT;
  /* our Session Records are per driver, so afsocket has to call
   * restart_with_state() */
  self->factory.stateful = TRUE;

  return self;
}

static void
_altp_receiver_context_free(AltpReceiverContext *self)
{
  /* the Connections hold references of their own, so the registry survives a
   * configuration reload that destroys us while they live on */
  altp_session_registry_unref(self->registry);
  g_free(self->persist_name);
  g_free(self);
}

/* Called by log_proto_server_options_destroy() through the destroy hook. */
static void
_options_destroy(LogProtoServerOptions *s)
{
  AltpProtoServerOptions *self = (AltpProtoServerOptions *) s;

  _altp_receiver_context_free(self->context);
  self->context = NULL;
}

AltpProtoServerOptions *
altp_proto_server_options_defaults(LogProtoServerOptions *s)
{
  AltpProtoServerOptions *self = (AltpProtoServerOptions *) s;

  /* log_proto_server_options_defaults() only clears the embedded super */
  self->altp.ack_timeout = ALTP_DEFAULT_ACK_TIMEOUT;
  self->altp.ack_timeout_action = ALTP_ACK_TIMEOUT_ACTION_CLOSE;
  self->altp.session_expiration = ALTP_DEFAULT_SESSION_EXPIRATION;
  self->altp.max_sessions = ALTP_DEFAULT_MAX_SESSIONS;
  self->altp.tls_policy = ALTP_TLS_POLICY_AUTO;
  self->altp.allow_compression = FALSE;
  self->context = _altp_receiver_context_new();
  self->super.destroy = _options_destroy;

  /* a Session Record needs durability as an absolute prefix count (10.1),
   * which is what the consecutive ack tracker reports */
  log_proto_server_options_set_ack_tracker_factory(&self->super, consecutive_ack_tracker_factory_new());

  return self;
}

LogProtoServerFactory *
altp_proto_server_options_get_factory(AltpProtoServerOptions *self)
{
  return &self->context->factory;
}

/****************************************************************************
 * The Sender side of the transport
 ****************************************************************************/

/* The factory below is only ever handed out for a LogProtoClientOptionsStorage
 * the grammar initialized, so the cast is safe. */
static LogProtoClient *
_construct_client_proto(LogTransport *transport, const LogProtoClientOptions *options)
{
  const AltpProtoClientOptions *self = (const AltpProtoClientOptions *) options;

  return log_proto_altp_client_new(transport, options, &self->altp);
}

/* One factory serves every transport(altp) destination: unlike a Receiver, a
 * Sender keeps nothing outside its options and its persistent state.
 */
static LogProtoClientFactory altp_proto_client_factory =
{
  .construct = _construct_client_proto,
  .default_inet_port = ALTP_DEFAULT_PORT,
  /* Our Session ID and frames_sent come from the persistent state of the
   * driver, so afsocket has to call restart_with_state() -- and, just as
   * importantly, must not rewind our flow control backlog at initialization
   * time: those Frames are retained until the next SYNC reconciles them (10.2).
   */
  .stateful = TRUE,
};

AltpProtoClientOptions *
altp_proto_client_options_defaults(LogProtoClientOptions *s)
{
  AltpProtoClientOptions *self = (AltpProtoClientOptions *) s;

  /* nobody calls log_proto_client_options_defaults(), the storage of the
   * driver simply arrives zeroed */
  self->altp.ack_timeout = ALTP_DEFAULT_ACK_TIMEOUT;
  self->altp.max_frame_size = ALTP_SENDER_DEFAULT_MAX_FRAME_SIZE;
  self->altp.compression = FALSE;
  self->altp.compression_level = ALTP_SENDER_DEFAULT_COMPRESSION_LEVEL;

  return self;
}

LogProtoClientFactory *
altp_proto_client_options_get_factory(AltpProtoClientOptions *self)
{
  return &altp_proto_client_factory;
}
