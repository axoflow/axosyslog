/*
 * Copyright (c) 2018 Balabit
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

#include "http-source.h"
#include "http/logproto-http-server.h"
#include "socket/socket-options-inet.h"
#include "socket/transport-mapper-inet.h"
#include "socket/afinet.h"
#include "host-resolve.h"
#include "messages.h"
#include "stats/stats-registry.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>

#define HTTP_PORT 80
#define HTTPS_PORT 443

static gboolean
http_transport_mapper_apply_transport(TransportMapper *s, GlobalConfig *cfg)
{
  TransportMapperInet *self = (TransportMapperInet *) s;
  const gchar *transport;

  if (!transport_mapper_apply_transport_method(s, cfg))
    return FALSE;

  transport = self->super.transport;
  if (strcasecmp(transport, "tls") == 0)
    {
      self->server_port = HTTPS_PORT;
      self->super.logproto = "text";
      self->super.sock_type = SOCK_STREAM;
      self->super.sock_proto = IPPROTO_TCP;
      self->require_tls_configuration = TRUE;
    }
  else if (strcasecmp(transport, "tcp") == 0)
    {
      self->server_port = HTTP_PORT;
      self->super.logproto = "text";
      self->super.sock_type = SOCK_STREAM;
      self->super.sock_proto = IPPROTO_TCP;
    }
  else
    {
      self->super.logproto = self->super.transport;
      self->super.sock_type = SOCK_STREAM;
      self->super.sock_proto = IPPROTO_TCP;
      self->server_port = HTTP_PORT;
      self->allow_tls_configuration = TRUE;
    }

  g_assert(self->server_port != 0);

  if (!transport_mapper_inet_validate_tls_options(self))
    return FALSE;

  return TRUE;
}

TransportMapper *
http_transport_mapper_new(void)
{
  TransportMapperInet *self = transport_mapper_inet_new_instance("tcp");

  self->super.apply_transport = http_transport_mapper_apply_transport;
  self->super.stats_source = stats_register_type("network");
  return &self->super;
}

void
http_sd_set_localport(LogDriver *s, gchar *service)
{
  HTTPSourceDriver *self = (HTTPSourceDriver *) s;

  if (self->bind_port)
    g_free(self->bind_port);
  self->bind_port = g_strdup(service);
}

void
http_sd_set_localip(LogDriver *s, gchar *ip)
{
  HTTPSourceDriver *self = (HTTPSourceDriver *) s;

  if (self->bind_ip)
    g_free(self->bind_ip);
  self->bind_ip = g_strdup(ip);
}

void
http_sd_set_tls_context(LogDriver *s, TLSContext *tls_context)
{
  HTTPSourceDriver *self = (HTTPSourceDriver *) s;

  transport_mapper_inet_set_tls_context((TransportMapperInet *) self->super.transport_mapper, tls_context);
}

static LogProtoServer *
http_sd_construct_proto(AFSocketSourceConnection *sc, StatsClusterKeyBuilder *kb)
{
  HTTPSourceDriver *self = (HTTPSourceDriver *) sc->owner;

  LogProtoServer *proto = log_proto_http_server_new(NULL, &self->super.reader_options.proto_options.super);

  log_proto_http_server_set_extract_log_messages(proto,
                                                 (LPHTTPExtractLogMessagesFunc) self->extract_log_messages, sc);
  log_proto_http_server_set_create_response(proto,
                                            (LPHTTPCreateResponseFunc) self->create_response, sc);

  return proto;
}

static gboolean
http_sd_setup_addresses(AFSocketSourceDriver *s)
{
  HTTPSourceDriver *self = (HTTPSourceDriver *) s;

  if (!afsocket_sd_setup_addresses_method(s))
    return FALSE;

  g_sockaddr_unref(self->super.bind_addr);

  if (!resolve_hostname_to_sockaddr(&self->super.bind_addr, self->super.transport_mapper->address_family, self->bind_ip))
    return FALSE;

  if (!self->bind_port)
    g_sockaddr_set_port(self->super.bind_addr, transport_mapper_inet_get_server_port(self->super.transport_mapper));
  else
    g_sockaddr_set_port(self->super.bind_addr, afinet_lookup_service(self->super.transport_mapper, self->bind_port));

  self->super.activate_listener = (g_sockaddr_get_port(self->super.bind_addr) != 0);
  return TRUE;
}

gboolean
http_sd_init_method(LogPipe *s)
{
  HTTPSourceDriver *self = (HTTPSourceDriver *) s;
  GlobalConfig *cfg = log_pipe_get_config(s);

  LogProtoServerOptions *proto_options = &self->super.reader_options.proto_options.super;
  if (proto_options->init_buffer_size == -1)
    {
      gint max_msg_size = proto_options->max_msg_size != -1 ? proto_options->max_msg_size : cfg->log_msg_size;
      proto_options->init_buffer_size = max_msg_size * 1000;
    }

  if (self->super.reader_options.super.init_window_size == -1)
    {
      self->super.reader_options.super.init_window_size =
        atomic_gssize_get(&self->super.max_connections) * cfg->min_iw_size_per_reader;
    }

  return afsocket_sd_init_method(s);
}

void
http_sd_free_method(LogPipe *s)
{
  HTTPSourceDriver *self = (HTTPSourceDriver *) s;

  g_free(self->bind_ip);
  g_free(self->bind_port);
  afsocket_sd_free_method(s);
}

void
http_sd_init_instance(HTTPSourceDriver *self, SocketOptions *socket_options,
                      TransportMapper *transport_mapper, GlobalConfig *cfg)
{
  afsocket_sd_init_instance(&self->super, socket_options, transport_mapper, "http", cfg);
  self->super.super.super.super.init = http_sd_init_method;
  self->super.super.super.super.free_fn = http_sd_free_method;
  self->super.setup_addresses = http_sd_setup_addresses;
  self->super.construct_proto = http_sd_construct_proto;

  atomic_gssize_set(&self->super.max_connections, 100);
}
