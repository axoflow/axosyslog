/*
 * Copyright (c) 2002-2012 Balabit
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

#ifndef AFSOCKET_DEST_H_INCLUDED
#define AFSOCKET_DEST_H_INCLUDED

#include "afsocket.h"
#include "socket/socket-options.h"
#include "socket/transport-mapper.h"
#include "driver.h"
#include "logwriter.h"
#include "logbalancer.h"

#include <iv.h>

typedef struct _AFSocketDestDriver AFSocketDestDriver;
typedef struct _AFSocketDestConnection AFSocketDestConnection;
typedef struct _AFSocketDestKeptAliveConnection AFSocketDestKeptAliveConnection;

typedef enum
{
  AFSOCKET_DD_CONNECTION_FAILURE_REFUSED,
  AFSOCKET_DD_CONNECTION_FAILURE_TIMEOUT,
  AFSOCKET_DD_CONNECTION_FAILURE_NO_ROUTE,
  AFSOCKET_DD_CONNECTION_FAILURE_RESET,
  AFSOCKET_DD_CONNECTION_FAILURE_EOF,
  AFSOCKET_DD_CONNECTION_FAILURE_RESOLVE,
  AFSOCKET_DD_CONNECTION_FAILURE_ERROR,
  AFSOCKET_DD_CONNECTION_FAILURE_MAX
} AFSocketDestConnectionFailure;

/* A single connection of the destination, with its own writer and queue.
 * The destination distributes messages between its connections. */
struct _AFSocketDestConnection
{
  AFSocketDestDriver *owner;
  gint index;

  gint fd;
  LogWriter *writer;
  GSockAddr *dest_addr;
  gboolean initialized;
  struct iv_fd connect_fd;
  struct iv_timer reconnect_timer;

  /* read by source threads, see afsocket_dd_queue() */
  gboolean currently_unreachable;
};

struct _AFSocketDestDriver
{
  LogDestDriver super;

  guint
  connections_kept_alive_across_reloads:1;
  LogWriterOptions writer_options;
  LogProtoClientFactory *proto_factory;

  GSockAddr *bind_addr;
  SocketOptions *socket_options;
  TransportMapper *transport_mapper;
  gchar *driver_name;

  AFSocketDestConnection *connections;
  gint num_connections;
  LogBalancerOptions balancer_options;
  LogBalancer *balancer;
  /* number of connections currently unreachable */
  gint unreachable_connections;

  struct
  {
    StatsCounterItem *output_unreachable;
    StatsCounterItem *output_connection_failures[AFSOCKET_DD_CONNECTION_FAILURE_MAX];
  } metrics;

  LogWriter *(*construct_writer)(AFSocketDestDriver *self);
  LogProtoClient *(*construct_proto)(AFSocketDestDriver *self, AFSocketDestConnection *conn, gint fd);
  gboolean (*setup_addresses)(AFSocketDestDriver *s, AFSocketDestConnection *conn);
  const gchar *(*get_dest_name)(const AFSocketDestDriver *s);

  void (*save_connection)(AFSocketDestDriver *s, AFSocketDestConnection *conn);
  gboolean (*should_restore_connection)(AFSocketDestDriver *s, AFSocketDestKeptAliveConnection *c);
  void (*restore_connection)(AFSocketDestDriver *s, AFSocketDestConnection *conn, AFSocketDestKeptAliveConnection *c);
};

struct _AFSocketDestKeptAliveConnection
{
  gchar *transport;
  gchar *proto;

  GSockAddr *dest_addr;
  LogWriter *writer;

  void (*free_fn)(AFSocketDestKeptAliveConnection *s);
};


static inline LogWriter *
afsocket_dd_construct_writer(AFSocketDestDriver *self)
{
  return self->construct_writer(self);
}

static inline gboolean
afsocket_dd_setup_addresses(AFSocketDestDriver *s, AFSocketDestConnection *conn)
{
  return s->setup_addresses(s, conn);
}

static inline const gchar *
afsocket_dd_get_dest_name(const AFSocketDestDriver *s)
{
  return s->get_dest_name(s);
}

LogWriter *afsocket_dd_construct_writer_method(AFSocketDestDriver *self);
LogProtoClient *afsocket_dd_construct_proto_method(AFSocketDestDriver *self, AFSocketDestConnection *conn, gint fd);
gboolean afsocket_dd_setup_addresses_method(AFSocketDestDriver *self, AFSocketDestConnection *conn);
void afsocket_dd_set_keep_alive(LogDriver *self, gint enable);
void afsocket_dd_set_num_connections(LogDriver *self, gint num_connections);
void afsocket_dd_set_partition_key_ref(LogDriver *self, LogTemplate *partition_key);
void afsocket_dd_set_batch_size(LogDriver *self, gint batch_size);
void afsocket_dd_init_instance(AFSocketDestDriver *self, SocketOptions *socket_options,
                               TransportMapper *transport_mapper, const gchar *driver_name,
                               GlobalConfig *cfg);
void afsocket_dd_reconnect(AFSocketDestConnection *conn);
void afsocket_dd_stop_watches(AFSocketDestConnection *conn);

gboolean afsocket_dd_init(LogPipe *s);
gboolean afsocket_dd_deinit(LogPipe *s);
void afsocket_dd_free(LogPipe *s);
void afsocket_dd_connected_with_fd(AFSocketDestConnection *conn, gint fd, GSockAddr *saddr);


void afsocket_dd_save_connection(AFSocketDestDriver *self, AFSocketDestConnection *conn,
                                 AFSocketDestKeptAliveConnection *c);
gboolean afsocket_dd_should_restore_connection_method(AFSocketDestDriver *self, AFSocketDestKeptAliveConnection *c);
void afsocket_dd_restore_connection_method(AFSocketDestDriver *self, AFSocketDestConnection *conn,
                                           AFSocketDestKeptAliveConnection *item);

void afsocket_kept_alive_connection_init_instance(AFSocketDestKeptAliveConnection *s,
                                                  const gchar *transport, const gchar *proto,
                                                  GSockAddr *dest_addr, LogWriter *writer);

void afsocket_kept_alive_connection_free_method(AFSocketDestKeptAliveConnection *s);

static inline void
afsocket_kept_alive_connection_free(AFSocketDestKeptAliveConnection *self)
{
  self->free_fn(self);
  g_free(self);
}

#endif
