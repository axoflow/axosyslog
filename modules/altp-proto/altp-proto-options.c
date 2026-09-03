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
#include "logproto-altp-server.h"

struct _AltpReceiverContext
{
  /* One factory per transport(altp(...)) occurrence, i.e. one per driver.  It
   * lives here and not as a static, because afsocket reads
   * default_inet_port, stateful and default_iw_size_per_connection off it and
   * constructs every Connection of the driver through it.
   *
   * Stage B: the Session Registry, the PersistState and the driver's
   * persistent name prefix, and the periodic Session Record expiry timer.
   */
  LogProtoServerFactory factory;
};

/* The factory of a receiver context is only ever handed out for a
 * LogProtoServerOptionsStorage the grammar initialized, so the cast is safe. */
static LogProtoServer *
_construct_proto(LogTransport *transport, const LogProtoServerOptions *options, StatsClusterKeyBuilder *kb)
{
  const AltpProtoServerOptions *self = (const AltpProtoServerOptions *) options;

  return log_proto_altp_server_new(transport, options, &self->altp, kb);
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
  self->altp.session_expiration = ALTP_DEFAULT_SESSION_EXPIRATION;
  self->altp.tls_policy = ALTP_TLS_POLICY_AUTO;
  self->context = _altp_receiver_context_new();
  self->super.destroy = _options_destroy;

  /* Stage B: the Session Records report durability as an absolute prefix
   * count, which is what a consecutive ack tracker gives us, so this is
   * where log_proto_server_options_set_ack_tracker_factory() installs
   * consecutive_ack_tracker_factory_new(). */

  return self;
}

LogProtoServerFactory *
altp_proto_server_options_get_factory(AltpProtoServerOptions *self)
{
  return &self->context->factory;
}
