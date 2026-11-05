/*
 * Copyright (c) 2002-2014 Balabit
 * Copyright (c) 1998-2012 Balázs Scheidler
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

#include "afsocket-dest.h"
#include "messages.h"
#include "logwriter.h"
#include "gsocket.h"
#include "stats/stats-registry.h"
#include "mainloop.h"
#include "timeutils/misc.h"
#include "hostname.h"
#include "persist-state.h"
#include "stats/stats-registry.h"
#include "stats/stats-cluster-single.h"

#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>

static const gchar *connection_failure_reasons[AFSOCKET_DD_CONNECTION_FAILURE_MAX] =
{
  [AFSOCKET_DD_CONNECTION_FAILURE_REFUSED] = "refused",
  [AFSOCKET_DD_CONNECTION_FAILURE_TIMEOUT] = "timeout",
  [AFSOCKET_DD_CONNECTION_FAILURE_NO_ROUTE] = "no-route",
  [AFSOCKET_DD_CONNECTION_FAILURE_RESET] = "reset",
  [AFSOCKET_DD_CONNECTION_FAILURE_EOF] = "eof",
  [AFSOCKET_DD_CONNECTION_FAILURE_RESOLVE] = "resolve",
  [AFSOCKET_DD_CONNECTION_FAILURE_ERROR] = "error",
};

static AFSocketDestConnectionFailure
_connection_failure_from_errno(gint error)
{
  switch (error)
    {
    case ECONNREFUSED:
      return AFSOCKET_DD_CONNECTION_FAILURE_REFUSED;
    case ETIMEDOUT:
      return AFSOCKET_DD_CONNECTION_FAILURE_TIMEOUT;
    case ENETUNREACH:
    case EHOSTUNREACH:
    case EHOSTDOWN:
      return AFSOCKET_DD_CONNECTION_FAILURE_NO_ROUTE;
    case ECONNRESET:
    case EPIPE:
      return AFSOCKET_DD_CONNECTION_FAILURE_RESET;
    default:
      return AFSOCKET_DD_CONNECTION_FAILURE_ERROR;
    }
}

static void
_count_connection_failure(AFSocketDestDriver *self, AFSocketDestConnectionFailure failure)
{
  stats_counter_inc(self->metrics.output_connection_failures[failure]);
}


void
afsocket_kept_alive_connection_init_instance(AFSocketDestKeptAliveConnection *s, const gchar *transport,
                                             const gchar *proto, GSockAddr *dest_addr, LogWriter *writer)
{
  s->transport = g_strdup(transport);
  s->proto = g_strdup(proto);
  s->writer = writer;
  s->dest_addr = g_sockaddr_ref(dest_addr);
  s->free_fn = afsocket_kept_alive_connection_free_method;
}

static AFSocketDestKeptAliveConnection *
_kept_alive_connection_new(const gchar *transport, const gchar *proto, GSockAddr *dest_addr, LogWriter *writer)
{
  AFSocketDestKeptAliveConnection *conn = g_new(AFSocketDestKeptAliveConnection, 1);
  afsocket_kept_alive_connection_init_instance(conn, transport, proto, dest_addr, writer);

  return conn;
}

void
afsocket_kept_alive_connection_free_method(AFSocketDestKeptAliveConnection *self)
{
  if (!self)
    return;

  if (self->writer)
    log_pipe_unref((LogPipe *) self->writer);

  g_sockaddr_unref(self->dest_addr);
  g_free(self->transport);
  g_free(self->proto);
}

static LogWriter *
_kept_alive_connection_steal_writer(AFSocketDestKeptAliveConnection *self)
{
  LogWriter *writer = self->writer;
  self->writer = NULL;

  return writer;
}

gboolean
afsocket_dd_should_restore_connection_method(AFSocketDestDriver *self, AFSocketDestKeptAliveConnection *c)
{
  return g_strcmp0(transport_mapper_get_transport(self->transport_mapper), c->transport) == 0
         && g_strcmp0(transport_mapper_get_logproto(self->transport_mapper), c->proto) == 0;
}

void
afsocket_dd_set_keep_alive(LogDriver *s, gboolean enable)
{
  AFSocketDestDriver *self = (AFSocketDestDriver *) s;

  self->connections_kept_alive_across_reloads = enable;
}

static const gchar *_module_name = "afsocket_dd";

static const gchar *
_get_module_identifier(const AFSocketDestDriver *self)
{
  static gchar module_identifier[128];

  g_snprintf(module_identifier, sizeof(module_identifier), "%s,%s",
             (self->transport_mapper->sock_type == SOCK_STREAM) ? "stream" : "dgram",
             afsocket_dd_get_dest_name(self));

  return self->super.super.super.persist_name ? self->super.super.super.persist_name
         : module_identifier;
}

static const gchar *
_get_legacy_module_identifier(const AFSocketDestDriver *self)
{
  static gchar legacy_module_identifier[128];
  const gchar *hostname = get_local_hostname_fqdn();

  g_snprintf(legacy_module_identifier, sizeof(legacy_module_identifier), "%s,%s,%s",
             (self->transport_mapper->sock_type == SOCK_STREAM) ? "stream" : "dgram",
             afsocket_dd_get_dest_name(self),
             hostname);

  return legacy_module_identifier;
}

static const gchar *
afsocket_dd_format_name(const LogPipe *s)
{
  const AFSocketDestDriver *self = (const AFSocketDestDriver *)s;
  static gchar persist_name[1024];
  g_snprintf(persist_name, sizeof(persist_name), "%s.(%s)", _module_name,
             _get_module_identifier(self));

  return persist_name;
}

/* The first connection uses the persist names of the single connection
 * destination, so that existing disk-buffers and kept alive connections are
 * picked up.  The rest of the connections get an index suffix. */
static const gchar *
_format_indexed_persist_name(gchar *buf, gsize buf_len, const gchar *kind, const AFSocketDestConnection *conn)
{
  if (conn->index == 0)
    g_snprintf(buf, buf_len, "%s_%s(%s)", _module_name, kind, _get_module_identifier(conn->owner));
  else
    g_snprintf(buf, buf_len, "%s_%s(%s).%d", _module_name, kind, _get_module_identifier(conn->owner), conn->index);
  return buf;
}

static const gchar *
afsocket_dd_format_qfile_name(const AFSocketDestConnection *conn)
{
  static gchar persist_name[1024];

  return _format_indexed_persist_name(persist_name, sizeof(persist_name), "qfile", conn);
}

static const gchar *
afsocket_dd_format_connections_name(const AFSocketDestConnection *conn)
{
  static gchar persist_name[1024];

  return _format_indexed_persist_name(persist_name, sizeof(persist_name), "connections", conn);
}

static const gchar *
afsocket_dd_format_legacy_connection_name(const AFSocketDestDriver *self)
{
  static gchar legacy_persist_name[1024];
  g_snprintf(legacy_persist_name, sizeof(legacy_persist_name), "%s_connection(%s)", _module_name,
             _get_legacy_module_identifier(self));

  return legacy_persist_name;
}

static gchar *
afsocket_dd_stats_instance(AFSocketDestDriver *self)
{
  static gchar buf[256];

  g_snprintf(buf, sizeof(buf), "%s,%s", self->transport_mapper->transport, afsocket_dd_get_dest_name(self));
  return buf;
}

/* AFSocketDestConnection */

static void _afsocket_dd_connection_in_progress(AFSocketDestConnection *conn);

static void
_connection_init_instance(AFSocketDestConnection *conn, AFSocketDestDriver *owner, gint index)
{
  conn->owner = owner;
  conn->index = index;
  conn->fd = -1;

  IV_FD_INIT(&conn->connect_fd);
  conn->connect_fd.cookie = conn;
  conn->connect_fd.handler_out = (void (*)(void *)) _afsocket_dd_connection_in_progress;

  IV_TIMER_INIT(&conn->reconnect_timer);
  conn->reconnect_timer.cookie = conn;
  conn->reconnect_timer.handler = (void (*)(void *)) afsocket_dd_reconnect;
}

static void
_connection_free(AFSocketDestConnection *conn)
{
  g_sockaddr_unref(conn->dest_addr);
  log_pipe_unref((LogPipe *) conn->writer);
}

/* runs in the main thread, source threads read currently_unreachable */
static void
_connection_set_unreachable(AFSocketDestConnection *conn, gboolean unreachable)
{
  AFSocketDestDriver *self = conn->owner;

  if (g_atomic_int_get(&conn->currently_unreachable) != unreachable)
    {
      g_atomic_int_set(&conn->currently_unreachable, unreachable);
      self->unreachable_connections += unreachable ? 1 : -1;
    }

  /* the destination is unreachable if none of its connections are reachable */
  stats_counter_set(self->metrics.output_unreachable, self->unreachable_connections == self->num_connections);
}

static gboolean
_connection_is_reachable(gint index, gpointer user_data)
{
  AFSocketDestDriver *self = (AFSocketDestDriver *) user_data;

  return !g_atomic_int_get(&self->connections[index].currently_unreachable);
}

static void
afsocket_dd_start_watches(AFSocketDestConnection *conn)
{
  main_loop_assert_main_thread();

  conn->connect_fd.fd = conn->fd;
  iv_fd_register(&conn->connect_fd);
}

void
afsocket_dd_stop_watches(AFSocketDestConnection *conn)
{
  main_loop_assert_main_thread();

  if (iv_fd_registered(&conn->connect_fd))
    {
      iv_fd_unregister(&conn->connect_fd);

      /* need to close the fd in this case as it wasn't established yet */
      msg_verbose("Closing connecting fd",
                  evt_tag_int("fd", conn->fd));
      close(conn->fd);
      conn->fd = -1;
    }
  if (iv_timer_registered(&conn->reconnect_timer))
    iv_timer_unregister(&conn->reconnect_timer);
}

static void
afsocket_dd_start_reconnect_timer(AFSocketDestConnection *conn)
{
  AFSocketDestDriver *self = conn->owner;

  main_loop_assert_main_thread();

  if (iv_timer_registered(&conn->reconnect_timer))
    iv_timer_unregister(&conn->reconnect_timer);
  iv_validate_now();

  conn->reconnect_timer.expires = iv_now;
  timespec_add_msec(&conn->reconnect_timer.expires, self->writer_options.time_reopen * 1000L);
  iv_timer_register(&conn->reconnect_timer);

  _connection_set_unreachable(conn, TRUE);
}

static gboolean
_update_legacy_connection_persist_name(AFSocketDestDriver *self)
{
  GlobalConfig *cfg = log_pipe_get_config(&self->super.super.super);
  const gchar *current_persist_name = afsocket_dd_format_connections_name(&self->connections[0]);
  const gchar *legacy_persist_name = afsocket_dd_format_legacy_connection_name(self);

  if (persist_state_entry_exists(cfg->state, current_persist_name))
    return TRUE;

  if (!persist_state_entry_exists(cfg->state, legacy_persist_name))
    return TRUE;

  return persist_state_move_entry(cfg->state, legacy_persist_name, current_persist_name);
}

LogProtoClient *
afsocket_dd_construct_proto_method(AFSocketDestDriver *self, AFSocketDestConnection *conn, gint fd)
{
  LogProtoClient *proto = log_proto_client_factory_construct(self->proto_factory, NULL,
                                                             &self->writer_options.proto_options.super);

  if (!transport_mapper_setup_stack(self->transport_mapper, &proto->transport_stack, fd))
    {
      log_proto_client_free(proto);
      return NULL;
    }

  return proto;
}

static gboolean
afsocket_dd_connected(AFSocketDestConnection *conn)
{
  AFSocketDestDriver *self = conn->owner;
  GlobalConfig *cfg = log_pipe_get_config(&self->super.super.super);
  gchar buf1[256], buf2[256];

  main_loop_assert_main_thread();

  _connection_set_unreachable(conn, FALSE);
  msg_notice("Syslog connection established",
             evt_tag_int("fd", conn->fd),
             evt_tag_str("server", g_sockaddr_format(conn->dest_addr, buf2, sizeof(buf2), GSA_FULL)),
             evt_tag_str("local", g_sockaddr_format(self->bind_addr, buf1, sizeof(buf1), GSA_FULL)));

  LogProtoClient *proto = self->construct_proto(self, conn, conn->fd);
  if (!proto)
    {
      _count_connection_failure(self, AFSOCKET_DD_CONNECTION_FAILURE_ERROR);
      return FALSE;
    }

  log_proto_client_restart_with_state(proto, cfg->state, afsocket_dd_format_connections_name(conn));
  log_writer_reopen(conn->writer, proto);
  return TRUE;
}

void
afsocket_dd_connected_with_fd(AFSocketDestConnection *conn, gint fd, GSockAddr *saddr)
{
  afsocket_dd_stop_watches(conn);
  g_sockaddr_unref(conn->dest_addr);
  conn->dest_addr = saddr;
  conn->fd = fd;
  if (!afsocket_dd_connected(conn))
    {
      close(conn->fd);
      conn->fd = -1;
      afsocket_dd_start_reconnect_timer(conn);
    }
}

static void
_afsocket_dd_connection_in_progress(AFSocketDestConnection *conn)
{
  AFSocketDestDriver *self = conn->owner;
  gchar buf[256];
  int error = 0;
  socklen_t errorlen = sizeof(error);

  if (iv_fd_registered(&conn->connect_fd))
    iv_fd_unregister(&conn->connect_fd);

  if (self->transport_mapper->sock_type == SOCK_STREAM)
    {
      if (getsockopt(conn->fd, SOL_SOCKET, SO_ERROR, &error, &errorlen) == -1)
        {
          gint getsockopt_error = errno;
          _count_connection_failure(self, _connection_failure_from_errno(getsockopt_error));
          msg_error("getsockopt(SOL_SOCKET, SO_ERROR) failed for connecting socket",
                    evt_tag_int("fd", conn->fd),
                    evt_tag_str("server", g_sockaddr_format(conn->dest_addr, buf, sizeof(buf), GSA_FULL)),
                    evt_tag_errno(EVT_TAG_OSERROR, getsockopt_error),
                    evt_tag_int("time_reopen", self->writer_options.time_reopen));
          goto error_reconnect;
        }
      if (error)
        {
          _count_connection_failure(self, _connection_failure_from_errno(error));
          msg_error("Syslog connection failed",
                    evt_tag_int("fd", conn->fd),
                    evt_tag_str("server", g_sockaddr_format(conn->dest_addr, buf, sizeof(buf), GSA_FULL)),
                    evt_tag_errno(EVT_TAG_OSERROR, error),
                    evt_tag_int("time_reopen", self->writer_options.time_reopen));
          goto error_reconnect;
        }
    }

  if (afsocket_dd_connected(conn))
    return;

error_reconnect:
  close(conn->fd);
  conn->fd = -1;
  afsocket_dd_start_reconnect_timer(conn);
}

static gboolean
afsocket_dd_start_connect(AFSocketDestConnection *conn)
{
  AFSocketDestDriver *self = conn->owner;
  int sock, rc;
  gchar buf1[MAX_SOCKADDR_STRING], buf2[MAX_SOCKADDR_STRING];

  main_loop_assert_main_thread();

  if (log_writer_opened(conn->writer))
    return TRUE;

  g_assert(self->transport_mapper->transport);
  g_assert(self->bind_addr);
  g_assert(conn->dest_addr);

  if (!transport_mapper_open_socket(self->transport_mapper, self->socket_options, self->bind_addr, conn->dest_addr,
                                    AFSOCKET_DIR_SEND, &sock))
    {
      _count_connection_failure(self, AFSOCKET_DD_CONNECTION_FAILURE_ERROR);
      return FALSE;
    }

  if (!socket_options_setup_peer_socket(self->socket_options, sock, conn->dest_addr))
    {
      _count_connection_failure(self, AFSOCKET_DD_CONNECTION_FAILURE_ERROR);
      return FALSE;
    }

  rc = g_connect(sock, conn->dest_addr);
  if (rc == G_IO_STATUS_NORMAL)
    {
      conn->fd = sock;
      if (!afsocket_dd_connected(conn))
        {
          close(conn->fd);
          conn->fd = -1;
          return FALSE;
        }
    }
  else if (rc == G_IO_STATUS_ERROR && errno == EINPROGRESS)
    {
      /* we must wait until connect succeeds */

      conn->fd = sock;
      afsocket_dd_start_watches(conn);
    }
  else
    {
      /* error establishing connection */
      gint error = errno;
      _count_connection_failure(self, _connection_failure_from_errno(error));
      msg_error("Connection failed",
                evt_tag_int("fd", sock),
                evt_tag_str("server", g_sockaddr_format(conn->dest_addr, buf2, sizeof(buf2), GSA_FULL)),
                evt_tag_str("local", g_sockaddr_format(self->bind_addr, buf1, sizeof(buf1), GSA_FULL)),
                evt_tag_errno(EVT_TAG_OSERROR, error));
      close(sock);
      return FALSE;
    }

  return TRUE;
}

void
afsocket_dd_reconnect(AFSocketDestConnection *conn)
{
  AFSocketDestDriver *self = conn->owner;
  gboolean addresses_resolved = afsocket_dd_setup_addresses(self, conn);

  if (!addresses_resolved)
    _count_connection_failure(self, AFSOCKET_DD_CONNECTION_FAILURE_RESOLVE);

  if (!addresses_resolved || !afsocket_dd_start_connect(conn))
    {
      msg_error("Initiating connection failed, reconnecting",
                evt_tag_int("time_reopen", self->writer_options.time_reopen));
      afsocket_dd_start_reconnect_timer(conn);
    }
}

static gboolean
afsocket_dd_setup_proto_factory(AFSocketDestDriver *self)
{
  GlobalConfig *cfg = log_pipe_get_config(&self->super.super.super);

  if (!self->proto_factory)
    self->proto_factory = log_proto_client_get_factory(&cfg->plugin_context, self->transport_mapper->logproto);

  if (!self->proto_factory)
    {
      msg_error("Unknown value specified in the transport() option, no such LogProto plugin found",
                evt_tag_str("transport", self->transport_mapper->logproto));
      return FALSE;
    }

  return TRUE;
}

static void
_afsocket_dd_set_flush_lines(AFSocketDestDriver *self, gboolean explicit)
{
  GlobalConfig *cfg = log_pipe_get_config(&self->super.super.super);

  /* each message has to remain a separate datagram */
  if (self->transport_mapper->sock_type == SOCK_DGRAM)
    {
      self->writer_options.proto_options.super.flush_lines = 1;
      return;
    }

  if (cfg_is_config_version_older(cfg, VERSION_VALUE_4_26) && !explicit)
    {
      msg_warning_once("WARNING: with " VERSION_4_26 ", network() and tcp() destinations using "
                       "a stream or TLS transport coalesce outgoing writes (flush-lines() defaults "
                       "to 100) for a possible higher throughput. Bump @version to 4.26 or set "
                       "flush-lines() explicitly to adopt it now and silence this warning. "
                       "Retaining the pre-4.26 one write() per message behavior for now");
      self->writer_options.proto_options.super.flush_lines = 1;
      return;
    }
}

static gboolean
afsocket_dd_setup_writer_options(AFSocketDestDriver *self)
{
  GlobalConfig *cfg = log_pipe_get_config(&self->super.super.super);

  gboolean flush_lines_explicit = (self->writer_options.proto_options.super.flush_lines != -1);

  log_writer_options_init(&self->writer_options, cfg, 0);

  _afsocket_dd_set_flush_lines(self, flush_lines_explicit);

  return TRUE;
}

static gboolean
afsocket_dd_setup_transport(AFSocketDestDriver *self)
{
  GlobalConfig *cfg = log_pipe_get_config(&self->super.super.super);

  if (!transport_mapper_apply_transport(self->transport_mapper, cfg))
    return FALSE;

  if (!afsocket_dd_setup_proto_factory(self))
    return FALSE;

  if (!afsocket_dd_setup_writer_options(self))
    return FALSE;
  return TRUE;
}

gboolean
afsocket_dd_setup_addresses_method(AFSocketDestDriver *self, AFSocketDestConnection *conn)
{
  return TRUE;
}

static gboolean
_afsocket_dd_try_to_restore_connection_state(AFSocketDestConnection *conn)
{
  AFSocketDestDriver *self = conn->owner;

  /* If we are reinitializing an old config, an existing writer may be present */
  if (conn->writer)
    return TRUE;

  AFSocketDestKeptAliveConnection *item = cfg_persist_config_fetch(log_pipe_get_config(&self->super.super.super),
                                          afsocket_dd_format_connections_name(conn));

  /* We don't have an item stored in the reload cache, which means */
  /* it is the first time when we try to initialize the writer */
  if (!item)
    return FALSE;

  if (!self->should_restore_connection(self, item))
    {
      afsocket_kept_alive_connection_free(item);
      return FALSE;
    }

  self->restore_connection(self, conn, item);
  afsocket_kept_alive_connection_free(item);
  return TRUE;
}

void
afsocket_dd_restore_connection_method(AFSocketDestDriver *self, AFSocketDestConnection *conn,
                                      AFSocketDestKeptAliveConnection *item)
{
  conn->writer = _kept_alive_connection_steal_writer(item);
  conn->dest_addr = g_sockaddr_ref(item->dest_addr);
}

LogWriter *
afsocket_dd_construct_writer_method(AFSocketDestDriver *self)
{
  guint32 writer_flags = 0;

  writer_flags |= LW_FORMAT_PROTO;

  LogWriter *writer = log_writer_new(writer_flags, self->super.super.super.cfg);
  log_pipe_set_options((LogPipe *) writer, &self->super.super.super.options);

  return writer;
}

static void
_init_stats_key_builders(AFSocketDestConnection *conn, StatsClusterKeyBuilder **writer_sck_builder,
                         StatsClusterKeyBuilder **driver_sck_builder, StatsClusterKeyBuilder **queue_sck_builder)
{
  AFSocketDestDriver *self = conn->owner;

  *writer_sck_builder = stats_cluster_key_builder_new();
  stats_cluster_key_builder_add_label(*writer_sck_builder, stats_cluster_label("driver", self->driver_name));
  stats_cluster_key_builder_add_legacy_label(*writer_sck_builder, stats_cluster_label("transport",
                                             self->transport_mapper->transport));
  stats_cluster_key_builder_add_legacy_label(*writer_sck_builder, stats_cluster_label("address",
                                             afsocket_dd_get_dest_name(self)));

  *driver_sck_builder = stats_cluster_key_builder_new();
  stats_cluster_key_builder_add_label(*driver_sck_builder, stats_cluster_label("driver", self->driver_name));
  stats_cluster_key_builder_add_label(*driver_sck_builder, stats_cluster_label("id", self->super.super.id));
  stats_cluster_key_builder_add_legacy_label(*driver_sck_builder, stats_cluster_label("transport",
                                             self->transport_mapper->transport));
  stats_cluster_key_builder_add_legacy_label(*driver_sck_builder, stats_cluster_label("address",
                                             afsocket_dd_get_dest_name(self)));
  stats_cluster_key_builder_set_legacy_alias(*driver_sck_builder,
                                             self->writer_options.stats_source | SCS_DESTINATION,
                                             self->super.super.id, afsocket_dd_stats_instance(self));

  *queue_sck_builder = stats_cluster_key_builder_new();
  stats_cluster_key_builder_add_label(*queue_sck_builder, stats_cluster_label("driver", self->driver_name));
  stats_cluster_key_builder_add_label(*queue_sck_builder, stats_cluster_label("id", self->super.super.id));
  stats_cluster_key_builder_add_legacy_label(*queue_sck_builder, stats_cluster_label("transport",
                                             self->transport_mapper->transport));
  stats_cluster_key_builder_add_legacy_label(*queue_sck_builder, stats_cluster_label("address",
                                             afsocket_dd_get_dest_name(self)));

  /* the queues of a single connection destination keep their labels */
  if (self->num_connections > 1)
    {
      gchar index_str[16];
      g_snprintf(index_str, sizeof(index_str), "%d", conn->index);
      stats_cluster_key_builder_add_label(*queue_sck_builder, stats_cluster_label("connection", index_str));
    }
}

static void
afsocket_dd_register_stats(AFSocketDestDriver *self)
{
  StatsClusterLabel labels[] =
  {
    stats_cluster_label("id", self->super.super.id),
    stats_cluster_label("driver", self->driver_name),
    stats_cluster_label("transport", self->transport_mapper->transport),
    stats_cluster_label("address", afsocket_dd_get_dest_name(self)),
    stats_cluster_label("reason", NULL),
  };
  const gsize reason_label = G_N_ELEMENTS(labels) - 1;

  gint level = log_pipe_is_internal(&self->super.super.super) ? STATS_LEVEL3 : STATS_LEVEL0;
  StatsClusterKey sc_key;
  stats_cluster_single_key_set(&sc_key, METRIC(output_unreachable), labels, reason_label);

  stats_lock();
  stats_register_counter(level, &sc_key, SC_TYPE_SINGLE_VALUE, &self->metrics.output_unreachable);
  for (gint i = 0; i < AFSOCKET_DD_CONNECTION_FAILURE_MAX; i++)
    {
      labels[reason_label] = stats_cluster_label("reason", connection_failure_reasons[i]);
      stats_cluster_single_key_set(&sc_key, METRIC(output_connection_failures_total), labels, G_N_ELEMENTS(labels));
      stats_register_counter(level, &sc_key, SC_TYPE_SINGLE_VALUE, &self->metrics.output_connection_failures[i]);
    }
  stats_unlock();
}

static void
afsocket_dd_unregister_stats(AFSocketDestDriver *self)
{
  StatsClusterLabel labels[] =
  {
    stats_cluster_label("id", self->super.super.id),
    stats_cluster_label("driver", self->driver_name),
    stats_cluster_label("transport", self->transport_mapper->transport),
    stats_cluster_label("address", afsocket_dd_get_dest_name(self)),
    stats_cluster_label("reason", NULL),
  };
  const gsize reason_label = G_N_ELEMENTS(labels) - 1;

  StatsClusterKey sc_key;
  stats_cluster_single_key_set(&sc_key, METRIC(output_unreachable), labels, reason_label);

  stats_lock();
  stats_unregister_counter(&sc_key, SC_TYPE_SINGLE_VALUE, &self->metrics.output_unreachable);
  for (gint i = 0; i < AFSOCKET_DD_CONNECTION_FAILURE_MAX; i++)
    {
      labels[reason_label] = stats_cluster_label("reason", connection_failure_reasons[i]);
      stats_cluster_single_key_set(&sc_key, METRIC(output_connection_failures_total), labels, G_N_ELEMENTS(labels));
      stats_unregister_counter(&sc_key, SC_TYPE_SINGLE_VALUE, &self->metrics.output_connection_failures[i]);
    }
  stats_unlock();
}

static gboolean
afsocket_dd_setup_writer(AFSocketDestConnection *conn)
{
  AFSocketDestDriver *self = conn->owner;
  gboolean kept_alive_connection = _afsocket_dd_try_to_restore_connection_state(conn);

  if (!conn->writer)
    {
      /* NOTE: we open our writer with no fd, so we can send messages down there
       * even while the connection is not established */

      conn->writer = afsocket_dd_construct_writer(self);
    }

  StatsClusterKeyBuilder *writer_sck_builder;
  StatsClusterKeyBuilder *driver_sck_builder;
  StatsClusterKeyBuilder *queue_sck_builder;
  _init_stats_key_builders(conn, &writer_sck_builder, &driver_sck_builder, &queue_sck_builder);

  log_pipe_set_config((LogPipe *)conn->writer, log_pipe_get_config(&self->super.super.super));
  log_writer_set_options(conn->writer, &self->super.super.super,
                         &self->writer_options,
                         self->super.super.id,
                         writer_sck_builder);

  log_writer_set_queue(conn->writer, NULL);
  gint stats_level = log_pipe_is_internal(&self->super.super.super) ? STATS_LEVEL3 : self->writer_options.stats_level;
  LogQueue *queue = log_dest_driver_acquire_queue(&self->super, afsocket_dd_format_qfile_name(conn),
                                                  stats_level, driver_sck_builder, queue_sck_builder);
  log_writer_set_queue(conn->writer, queue);
  log_dest_driver_register_rejected_counter(&self->super, stats_level, driver_sck_builder);

  stats_cluster_key_builder_free(queue_sck_builder);
  stats_cluster_key_builder_free(driver_sck_builder);

  if (!log_pipe_init((LogPipe *) conn->writer))
    {
      log_pipe_unref((LogPipe *) conn->writer);
      conn->writer = NULL;
      return FALSE;
    }

  /* the first connection is our pipe_next, the rest are fed directly by
   * afsocket_dd_queue() */
  if (conn->index == 0)
    log_pipe_append(&self->super.super.super, (LogPipe *) conn->writer);

  if (kept_alive_connection)
    {
      LogProtoClient *proto = log_writer_steal_proto(conn->writer);

      if (proto)
        {
          conn->fd = log_proto_client_get_fd(proto);
          log_writer_reopen(conn->writer, proto);
        }
    }
  conn->initialized = TRUE;
  return TRUE;
}

static gboolean
_setup_writers(AFSocketDestDriver *self)
{
  for (gint i = 0; i < self->num_connections; i++)
    {
      if (!afsocket_dd_setup_writer(&self->connections[i]))
        return FALSE;
    }
  return TRUE;
}

static gboolean
_finalize_init(gpointer arg)
{
  AFSocketDestDriver *self = (AFSocketDestDriver *)arg;

  for (gint i = 0; i < self->num_connections; i++)
    afsocket_dd_reconnect(&self->connections[i]);
  return TRUE;
}

static gboolean
_dd_init_stream(AFSocketDestDriver *self)
{
  if (!_setup_writers(self))
    return FALSE;

  return transport_mapper_async_init(self->transport_mapper, _finalize_init, self);
}

static gboolean
_dd_init_dgram(AFSocketDestDriver *self)
{
  if (!transport_mapper_init(self->transport_mapper))
    {
      return FALSE;
    }

  if (!_setup_writers(self))
    {
      return FALSE;
    }

  return _finalize_init(self);
}

static void
_dd_rewind_stateless_proto_backlog(AFSocketDestDriver *self)
{
  if (log_proto_client_factory_is_proto_stateful(self->proto_factory))
    return;

  for (gint i = 0; i < self->num_connections; i++)
    log_writer_msg_rewind(self->connections[i].writer);
}

static gboolean
_dd_init_socket(AFSocketDestDriver *self)
{
  switch (self->transport_mapper->sock_type)
    {
    case SOCK_STREAM:
      return _dd_init_stream(self);

    case SOCK_DGRAM:
    default:
      return _dd_init_dgram(self);
    }
}

static gboolean
_validate_connections(AFSocketDestDriver *self)
{
  if (self->num_connections > 1 && self->transport_mapper->sock_type != SOCK_STREAM)
    {
      msg_error("connections() greater than 1 is only supported with stream based transports",
                evt_tag_int("connections", self->num_connections),
                log_expr_node_location_tag(self->super.super.super.expr_node));
      return FALSE;
    }
  return TRUE;
}

static void
_init_connections(AFSocketDestDriver *self)
{
  /* connections are kept when an old configuration is reinitialized */
  if (self->connections)
    return;

  self->connections = g_new0(AFSocketDestConnection, self->num_connections);
  for (gint i = 0; i < self->num_connections; i++)
    _connection_init_instance(&self->connections[i], self, i);
}

static gboolean
_init_balancer(AFSocketDestDriver *self)
{
  GlobalConfig *cfg = log_pipe_get_config(&self->super.super.super);

  if (!log_balancer_options_init(&self->balancer_options, cfg, self->super.super.super.expr_node))
    return FALSE;

  if (self->num_connections > 1)
    {
      self->balancer = log_balancer_new(&self->balancer_options, self->num_connections);
      log_balancer_set_availability_check(self->balancer, _connection_is_reachable, self);
    }
  return TRUE;
}

gboolean
afsocket_dd_init(LogPipe *s)
{
  AFSocketDestDriver *self = (AFSocketDestDriver *) s;

  if (!log_dest_driver_init_method(s) ||
      !afsocket_dd_setup_transport(self))
    {
      return FALSE;
    }

  if (!_validate_connections(self))
    return FALSE;

  _init_connections(self);

  if (!_init_balancer(self))
    return FALSE;

  if (!_update_legacy_connection_persist_name(self))
    return FALSE;

  afsocket_dd_register_stats(self);

  if (!_dd_init_socket(self))
    {
      return FALSE;
    }

  _dd_rewind_stateless_proto_backlog(self);

  return TRUE;
}

static void
afsocket_dd_stop_writer(AFSocketDestConnection *conn)
{
  if (conn->writer)
    log_pipe_deinit((LogPipe *) conn->writer);
}

void
afsocket_dd_save_connection(AFSocketDestDriver *self, AFSocketDestConnection *conn,
                            AFSocketDestKeptAliveConnection *c)
{
  GlobalConfig *cfg = log_pipe_get_config(&self->super.super.super);

  cfg_persist_config_add(cfg, afsocket_dd_format_connections_name(conn), c,
                         (GDestroyNotify) afsocket_kept_alive_connection_free);
  conn->writer = NULL;
}

static void
afsocket_dd_save_connection_method(AFSocketDestDriver *self, AFSocketDestConnection *conn)
{
  const gchar *transport = transport_mapper_get_transport(self->transport_mapper);
  const gchar *proto = transport_mapper_get_logproto(self->transport_mapper);
  AFSocketDestKeptAliveConnection *item = _kept_alive_connection_new(transport, proto, conn->dest_addr, conn->writer);

  afsocket_dd_save_connection(self, conn, item);
}

gboolean
afsocket_dd_deinit(LogPipe *s)
{
  AFSocketDestDriver *self = (AFSocketDestDriver *) s;

  for (gint i = 0; self->connections && i < self->num_connections; i++)
    {
      AFSocketDestConnection *conn = &self->connections[i];

      afsocket_dd_stop_watches(conn);
      afsocket_dd_stop_writer(conn);

      if (conn->initialized && self->connections_kept_alive_across_reloads)
        {
          self->save_connection(self, conn);
        }
    }

  g_clear_pointer(&self->balancer, log_balancer_free);
  afsocket_dd_unregister_stats(self);
  return log_dest_driver_deinit_method(s);
}

static AFSocketDestConnection *
_lookup_connection_by_writer(AFSocketDestDriver *self, gpointer writer)
{
  for (gint i = 0; i < self->num_connections; i++)
    {
      if ((gpointer) self->connections[i].writer == writer)
        return &self->connections[i];
    }
  return NULL;
}

static void
afsocket_dd_notify(LogPipe *s, gint notify_code, gpointer user_data)
{
  AFSocketDestDriver *self = (AFSocketDestDriver *) s;
  gchar buf[MAX_SOCKADDR_STRING];

  switch (notify_code)
    {
    case NC_CLOSE:
    case NC_WRITE_ERROR:
    {
      /* the writer sends notifications with itself as user_data */
      AFSocketDestConnection *conn = _lookup_connection_by_writer(self, user_data);

      if (!conn)
        break;

      _count_connection_failure(self, (notify_code == NC_CLOSE)
                                ? AFSOCKET_DD_CONNECTION_FAILURE_EOF
                                : _connection_failure_from_errno(log_writer_get_io_error(conn->writer)));
      log_writer_reopen(conn->writer, NULL);

      msg_notice((notify_code == NC_CLOSE) ? "Syslog connection closed" : "Syslog connection broken",
                 evt_tag_int("fd", conn->fd),
                 evt_tag_str("server", g_sockaddr_format(conn->dest_addr, buf, sizeof(buf), GSA_FULL)),
                 evt_tag_int("time_reopen", self->writer_options.time_reopen));
      afsocket_dd_start_reconnect_timer(conn);
      break;
    }
    default:
      break;
    }
}

void
afsocket_dd_free(LogPipe *s)
{
  AFSocketDestDriver *self = (AFSocketDestDriver *) s;

  g_free(self->driver_name);
  log_writer_options_destroy(&self->writer_options);
  g_sockaddr_unref(self->bind_addr);
  for (gint i = 0; self->connections && i < self->num_connections; i++)
    _connection_free(&self->connections[i]);
  g_free(self->connections);
  if (self->balancer)
    log_balancer_free(self->balancer);
  log_balancer_options_destroy(&self->balancer_options);
  transport_mapper_free(self->transport_mapper);
  socket_options_free(self->socket_options);
  log_dest_driver_free(s);
}

/* runs in the source thread */
static AFSocketDestConnection *
_select_connection(AFSocketDestDriver *self, LogMessage *msg, const LogPathOptions *path_options)
{
  /* prefer connections that are up, so that messages do not pile up in
   * the queue of a connection that is down */
  gint index = log_balancer_select(self->balancer, msg, TRUE);

  if (index < 0)
    {
      /* flags(destination-failover): let the next destination take it */
      if (path_options->destination_failover)
        return NULL;

      /* buffer the message in its connection (sticky) or any of them,
       * just like a single connection destination does */
      index = log_balancer_select(self->balancer, msg, FALSE);
    }
  return &self->connections[index];
}

static void
afsocket_dd_queue(LogPipe *s, LogMessage *msg, const LogPathOptions *path_options)
{
  AFSocketDestDriver *self = (AFSocketDestDriver *) s;

  if (self->num_connections == 1)
    {
      if (path_options->destination_failover && g_atomic_int_get(&self->connections[0].currently_unreachable))
        {
          log_dest_driver_reject_message(&self->super, msg, path_options);
          return;
        }
      log_dest_driver_queue_method(s, msg, path_options);
      return;
    }

  AFSocketDestConnection *conn = _select_connection(self, msg, path_options);
  if (!conn)
    {
      log_dest_driver_reject_message(&self->super, msg, path_options);
      return;
    }
  log_dest_driver_queue_to(&self->super, (LogPipe *) conn->writer, msg, path_options);
}

void
afsocket_dd_set_num_connections(LogDriver *s, gint num_connections)
{
  AFSocketDestDriver *self = (AFSocketDestDriver *) s;

  self->num_connections = num_connections;
}

void
afsocket_dd_set_partition_key_ref(LogDriver *s, LogTemplate *partition_key)
{
  AFSocketDestDriver *self = (AFSocketDestDriver *) s;

  log_balancer_options_set_partition_key_ref(&self->balancer_options, partition_key);
}

void
afsocket_dd_set_batch_size(LogDriver *s, gint batch_size)
{
  AFSocketDestDriver *self = (AFSocketDestDriver *) s;

  self->balancer_options.batch_size = batch_size;
}

void
afsocket_dd_init_instance(AFSocketDestDriver *self,
                          SocketOptions *socket_options,
                          TransportMapper *transport_mapper,
                          const gchar *driver_name,
                          GlobalConfig *cfg)
{
  log_dest_driver_init_instance(&self->super, cfg);

  log_writer_options_defaults(&self->writer_options);
  self->super.super.super.init = afsocket_dd_init;
  self->super.super.super.deinit = afsocket_dd_deinit;
  self->super.super.super.queue = afsocket_dd_queue;
  self->super.super.super.free_fn = afsocket_dd_free;
  self->super.super.super.notify = afsocket_dd_notify;
  self->super.super.super.generate_persist_name = afsocket_dd_format_name;
  self->setup_addresses = afsocket_dd_setup_addresses_method;
  self->construct_writer = afsocket_dd_construct_writer_method;
  self->construct_proto = afsocket_dd_construct_proto_method;
  self->should_restore_connection = afsocket_dd_should_restore_connection_method;
  self->restore_connection = afsocket_dd_restore_connection_method;
  self->save_connection = afsocket_dd_save_connection_method;
  self->transport_mapper = transport_mapper;
  self->socket_options = socket_options;
  self->connections_kept_alive_across_reloads = TRUE;
  self->driver_name = g_strdup(driver_name);

  self->num_connections = 1;
  log_balancer_options_defaults(&self->balancer_options);
  /* the input of a thread is sent in batches to connections, in line with
   * the default of flush-lines() */
  self->balancer_options.batch_size = 100;

  self->writer_options.mark_mode = MM_GLOBAL;
  self->writer_options.stats_level = STATS_LEVEL0;
  self->writer_options.stats_source = self->transport_mapper->stats_source;
}
