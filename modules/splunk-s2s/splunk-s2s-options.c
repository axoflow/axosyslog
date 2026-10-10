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

#include "splunk-s2s-options.h"
#include "splunk-s2s-proto-server.h"

#define SPLUNK_S2S_DEFAULT_PORT 9997

static void
_options_destroy(LogProtoServerOptions *s)
{
  SplunkS2SProtoServerOptions *self = (SplunkS2SProtoServerOptions *) s;

  multi_line_options_destroy(&self->multi_line_options);
}

SplunkS2SProtoServerOptions *
splunk_s2s_proto_server_options_defaults(LogProtoServerOptions *s)
{
  SplunkS2SProtoServerOptions *self = (SplunkS2SProtoServerOptions *) s;

  /* log_proto_server_options_defaults() only clears the embedded super */
  multi_line_options_defaults(&self->multi_line_options);
  self->multi_line_timeout = 0;
  self->super.destroy = _options_destroy;

  return self;
}

/* The factory below is only ever handed out for a LogProtoServerOptionsStorage
 * the grammar initialized, so the cast is safe. */
static LogProtoServer *
_construct_proto(LogTransport *transport, const LogProtoServerOptions *options, StatsClusterKeyBuilder *kb)
{
  const SplunkS2SProtoServerOptions *self = (const SplunkS2SProtoServerOptions *) options;
  LogProtoServer *proto = log_proto_splunk_s2s_server_new(transport, options);

  log_proto_splunk_s2s_server_set_multi_line(proto, &self->multi_line_options, self->multi_line_timeout);
  return proto;
}

static LogProtoServerFactory splunk_s2s_proto_server_factory =
{
  .construct = _construct_proto,
  .default_inet_port = SPLUNK_S2S_DEFAULT_PORT,
};

LogProtoServerFactory *
splunk_s2s_proto_server_get_factory(void)
{
  return &splunk_s2s_proto_server_factory;
}
