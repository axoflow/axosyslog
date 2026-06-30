/*
 * Copyright (c) 2026 Axoflow
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

#include "http-server-listener.h"
#include "http-source-config.h"
#include "mainloop-threaded-worker.h"
#include "messages.h"
#include "atomic.h"
#include "gsockaddr.h"
#include "gsocket.h"
#include "timeutils/misc.h"
#include "transport/transport-stack.h"
#include "transport/transport-socket.h"
#include "transport/transport-tls.h"
#include "transport/transport-factory-tls.h"

#include "llhttp.h"

#include <iv.h>
#include <iv_event.h>

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define HTTP_READ_BUFFER_SIZE 16384

typedef struct _PathRegistration
{
  HTTPServerRequestHandler handler;
  HTTPServerRequestCompletion completion;
  gpointer user_data;
  gsize max_request_size;
} PathRegistration;

struct HTTPServerListener
{
  gchar *key;
  gchar *bind_addr;
  gint port;
  gint user_count;
  gint connection_timeout;      /* seconds an idle/partial connection may live, 0 disables */
  TLSContext *tls_context;      /* non-NULL: terminate TLS on every connection */

  GMutex lock;
  GHashTable *paths;            /* path (owned gchar *) -> PathRegistration * */

  MainLoopThreadedWorker thread;
  gboolean thread_started;

  /* the following live on the listener thread once it is running */
  struct iv_fd listen_fd;
  struct iv_event wakeup_event;
  struct iv_event stop_event;
  GList *connections;           /* all open HTTPServerConnection * */
  GList *suspended;             /* connections paused for flow control */
  GAtomicCounter events_ready;  /* the cross-thread events are registered */
};

struct HTTPServerConnection
{
  HTTPServerListener *listener;
  gint fd;
  LogTransportStack stack;      /* socket transport, optionally with TLS on top */
  gboolean transport_active;    /* stack holds the fd and must be deinited */
  struct iv_fd iv_fd;
  struct iv_timer timer;
  struct iv_task free_task;
  GSockAddr *peer;

  llhttp_t parser;

  /* request being parsed */
  GString *url;
  GString *body;
  gchar *path;                  /* request path with the query string stripped */
  guint resp_status;            /* non-zero: an error response to send instead of dispatching */

  /* dispatch */
  gsize max_request_size;
  gsize offset;                 /* request handler progress into body */
  gboolean suspended;
  gboolean keep_alive;

  /* response */
  GString *write_buf;
  gsize write_offset;
  gboolean should_close;
  gboolean closing;
};

static llhttp_settings_t http_settings;

static void _conn_io_handler(void *cookie);
static void _conn_read(HTTPServerConnection *conn);
static void _conn_flush(HTTPServerConnection *conn);
static void _conn_update_watch(HTTPServerConnection *conn);
static void _conn_close(HTTPServerConnection *conn);
static HTTPServerRequestResult _conn_dispatch(HTTPServerConnection *conn);

/* --- per-connection inactivity timeout --------------------------------- */

static void
_conn_disarm_timer(HTTPServerConnection *conn)
{
  if (iv_timer_registered(&conn->timer))
    iv_timer_unregister(&conn->timer);
}

/* (re)arm the inactivity timer; called whenever the client makes progress or
 * the connection goes idle waiting for the next keep-alive request */
static void
_conn_touch_timer(HTTPServerConnection *conn)
{
  if (conn->listener->connection_timeout <= 0)
    return;

  _conn_disarm_timer(conn);
  iv_validate_now();
  conn->timer.expires = iv_now;
  timespec_add_msec(&conn->timer.expires, (gint64) conn->listener->connection_timeout * 1000);
  iv_timer_register(&conn->timer);
}

static void
_conn_timeout(void *cookie)
{
  HTTPServerConnection *conn = (HTTPServerConnection *) cookie;
  msg_debug("http(): closing inactive connection",
            evt_tag_int("port", conn->listener->port),
            evt_tag_int("timeout", conn->listener->connection_timeout));
  _conn_close(conn);
}

/* --- path registry lookups --------------------------------------------- */

static gboolean
_lookup_registration(HTTPServerListener *self, const gchar *path, PathRegistration *out)
{
  g_mutex_lock(&self->lock);
  PathRegistration *reg = path ? g_hash_table_lookup(self->paths, path) : NULL;
  if (reg)
    *out = *reg;
  g_mutex_unlock(&self->lock);
  return reg != NULL;
}

/* --- response handling ------------------------------------------------- */

static const gchar *
_reason_phrase(guint status)
{
  switch (status)
    {
    case 200:
      return "OK";
    case 400:
      return "Bad Request";
    case 404:
      return "Not Found";
    case 405:
      return "Method Not Allowed";
    case 413:
      return "Content Too Large";
    default:
      return "Internal Server Error";
    }
}

static const gchar *
_response_body(guint status)
{
  switch (status)
    {
    case 404:
      return "no source registered for this path\n";
    case 405:
      return "only POST is supported\n";
    case 413:
      return "request body too large\n";
    case 200:
      return "";
    default:
      return "internal server error\n";
    }
}

static void
_conn_queue_response(HTTPServerConnection *conn, guint status)
{
  const gchar *body = _response_body(status);
  gboolean keep_alive = conn->keep_alive && status == 200;

  g_string_append_printf(conn->write_buf,
                         "HTTP/1.1 %u %s\r\n"
                         "Content-Length: %zu\r\n"
                         "Connection: %s\r\n"
                         "\r\n",
                         status, _reason_phrase(status), strlen(body),
                         keep_alive ? "keep-alive" : "close");
  g_string_append(conn->write_buf, body);

  if (!keep_alive)
    conn->should_close = TRUE;
}

/*
 * Arm the event-loop watch for the connection's current I/O need.
 *
 * Normally the direction follows the logical state: wait for the fd to become
 * writable while a response is pending, readable otherwise.  The active
 * transport, however, can demand the opposite fd direction mid-stream (a TLS
 * read blocked on the handshake's write, or a write blocked on a
 * renegotiation read); when it does, that requirement wins.
 */
static void
_conn_update_watch(HTTPServerConnection *conn)
{
  if (conn->closing || conn->suspended)
    return;

  gboolean want_write = (conn->write_offset < conn->write_buf->len);

  switch (log_transport_stack_get_io_requirement(&conn->stack))
    {
    case LTIO_READ_WANTS_WRITE:
    case LTIO_WRITE_WANTS_WRITE:
      want_write = TRUE;
      break;
    case LTIO_READ_WANTS_READ:
    case LTIO_WRITE_WANTS_READ:
      want_write = FALSE;
      break;
    case LTIO_NOTHING:
    default:
      break;
    }

  iv_fd_set_handler_in(&conn->iv_fd, want_write ? NULL : _conn_io_handler);
  iv_fd_set_handler_out(&conn->iv_fd, want_write ? _conn_io_handler : NULL);
}

/* try to flush the pending response through the transport stack */
static void
_conn_flush(HTTPServerConnection *conn)
{
  while (conn->write_offset < conn->write_buf->len)
    {
      gssize n = log_transport_stack_write(&conn->stack, conn->write_buf->str + conn->write_offset,
                                           conn->write_buf->len - conn->write_offset);
      if (n > 0)
        {
          conn->write_offset += n;
          continue;
        }
      if (n < 0 && errno == EINTR)
        continue;
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        {
          /* short write: the transport tells us which direction to wait for */
          _conn_update_watch(conn);
          return;
        }
      _conn_close(conn);
      return;
    }

  g_string_truncate(conn->write_buf, 0);
  conn->write_offset = 0;

  if (conn->should_close)
    {
      _conn_close(conn);
      return;
    }

  /* keep-alive: wait for the next request, bounded by the inactivity timer */
  _conn_touch_timer(conn);
  _conn_update_watch(conn);
}

/* --- llhttp callbacks (run on the listener thread) --------------------- */

static int
_on_message_begin(llhttp_t *parser)
{
  HTTPServerConnection *conn = parser->data;

  g_string_truncate(conn->url, 0);
  g_string_truncate(conn->body, 0);
  g_free(conn->path);
  conn->path = NULL;
  conn->resp_status = 0;
  conn->offset = 0;
  conn->max_request_size = 0;
  return 0;
}

static int
_on_url(llhttp_t *parser, const char *at, size_t length)
{
  HTTPServerConnection *conn = parser->data;
  g_string_append_len(conn->url, at, length);
  return 0;
}

static int
_on_headers_complete(llhttp_t *parser)
{
  HTTPServerConnection *conn = parser->data;
  PathRegistration reg;

  /* strip the query string: routing matches the path only */
  const gchar *q = strchr(conn->url->str, '?');
  conn->path = q ? g_strndup(conn->url->str, q - conn->url->str) : g_strdup(conn->url->str);

  if (llhttp_get_method(parser) != HTTP_POST)
    conn->resp_status = 405;
  else if (!_lookup_registration(conn->listener, conn->path, &reg))
    conn->resp_status = 404;
  else
    conn->max_request_size = reg.max_request_size;

  return 0;
}

static int
_on_body(llhttp_t *parser, const char *at, size_t length)
{
  HTTPServerConnection *conn = parser->data;

  /* an error response is already decided; just drain the rest of the body */
  if (conn->resp_status != 0)
    return 0;

  if (conn->max_request_size && conn->body->len + length > conn->max_request_size)
    {
      conn->resp_status = 413;
      g_string_truncate(conn->body, 0);
      return 0;
    }

  g_string_append_len(conn->body, at, length);
  return 0;
}

static int
_on_message_complete(llhttp_t *parser)
{
  HTTPServerConnection *conn = parser->data;
  conn->keep_alive = llhttp_should_keep_alive(parser);

  if (conn->resp_status != 0)
    {
      _conn_queue_response(conn, conn->resp_status);
      return 0;
    }

  if (_conn_dispatch(conn) == HTTP_SERVER_REQUEST_SUSPENDED)
    {
      /* leave the connection paused; it is retried on wakeup */
      llhttp_pause(parser);
      return HPE_PAUSED;
    }

  return 0;
}

/* resolve the handler and run it from the saved offset; queues a 200 (or 404
 * if the path went away) when the body has been fully processed */
static HTTPServerRequestResult
_conn_dispatch(HTTPServerConnection *conn)
{
  PathRegistration reg;

  if (!_lookup_registration(conn->listener, conn->path, &reg))
    {
      _conn_queue_response(conn, 404);
      return HTTP_SERVER_REQUEST_COMPLETE;
    }

  HTTPServerRequestResult result =
    reg.handler(reg.user_data, conn, conn->body->str, conn->body->len, &conn->offset, conn->peer);

  if (result == HTTP_SERVER_REQUEST_COMPLETE)
    _conn_queue_response(conn, 200);

  return result;
}

/* --- connection lifecycle ---------------------------------------------- */

/* release the connection's event-loop watches and tear down its transport
 * stack (which owns and closes the fd) */
static void
_conn_shutdown_socket(HTTPServerConnection *conn)
{
  _conn_disarm_timer(conn);
  if (iv_fd_registered(&conn->iv_fd))
    iv_fd_unregister(&conn->iv_fd);
  if (conn->transport_active)
    {
      conn->transport_active = FALSE;
      log_transport_stack_deinit(&conn->stack);
      conn->fd = -1;
    }
}

static void
_conn_destroy(HTTPServerConnection *conn)
{
  if (iv_task_registered(&conn->free_task))
    iv_task_unregister(&conn->free_task);
  _conn_shutdown_socket(conn);

  g_string_free(conn->url, TRUE);
  g_string_free(conn->body, TRUE);
  g_string_free(conn->write_buf, TRUE);
  g_free(conn->path);
  g_sockaddr_unref(conn->peer);
  g_free(conn);
}

/* deferred free: keep the connection alive until the current callback unwinds */
static void
_conn_free_task(void *cookie)
{
  _conn_destroy((HTTPServerConnection *) cookie);
}

static void
_conn_close(HTTPServerConnection *conn)
{
  if (conn->closing)
    return;
  conn->closing = TRUE;

  _conn_shutdown_socket(conn);

  conn->listener->connections = g_list_remove(conn->listener->connections, conn);
  conn->listener->suspended = g_list_remove(conn->listener->suspended, conn);

  PathRegistration reg;
  if (conn->path && _lookup_registration(conn->listener, conn->path, &reg) && reg.completion)
    reg.completion(reg.user_data, conn);

  iv_task_register(&conn->free_task);
}

static void
_conn_read(HTTPServerConnection *conn)
{
  gchar buf[HTTP_READ_BUFFER_SIZE];

  gssize n = log_transport_stack_read(&conn->stack, buf, sizeof(buf), NULL);
  if (n == 0)
    {
      _conn_close(conn);
      return;
    }
  if (n < 0)
    {
      if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
        {
          /* nothing to read yet (or the TLS layer is mid-handshake): the
           * transport tells us which direction to wait for */
          _conn_update_watch(conn);
          return;
        }
      _conn_close(conn);
      return;
    }

  /* the client made progress: push back the inactivity deadline */
  _conn_touch_timer(conn);

  enum llhttp_errno err = llhttp_execute(&conn->parser, buf, n);

  if (conn->closing)
    return;

  if (err != HPE_OK && err != HPE_PAUSED)
    {
      _conn_queue_response(conn, 400);
      conn->should_close = TRUE;
    }

  if (conn->write_offset < conn->write_buf->len)
    {
      _conn_flush(conn);
      return;
    }

  _conn_update_watch(conn);
}

/*
 * Single I/O dispatcher used for both fd directions.  A TLS read may need the
 * fd to become writable and vice versa, so the watched direction does not map
 * one-to-one to read/write; we pick the operation from the logical state and
 * let the transport handle the rest.
 */
static void
_conn_io_handler(void *cookie)
{
  HTTPServerConnection *conn = (HTTPServerConnection *) cookie;

  if (conn->write_offset < conn->write_buf->len)
    _conn_flush(conn);
  else
    _conn_read(conn);
}

static void
_conn_error_handler(void *cookie)
{
  _conn_close((HTTPServerConnection *) cookie);
}

static void
_conn_start(HTTPServerConnection *conn)
{
  iv_fd_register(&conn->iv_fd);
  /* start out waiting for the client; reading drives the TLS handshake too */
  _conn_update_watch(conn);
  _conn_touch_timer(conn);
}

/* build the transport stack for the connection: a stream socket transport,
 * with a TLS transport switched in on top when the listener terminates TLS.
 * Returns FALSE (leaving the fd closed by the stack) if TLS setup fails. */
static gboolean
_conn_setup_transport(HTTPServerConnection *conn, gint fd)
{
  log_transport_stack_init(&conn->stack, log_transport_stream_socket_new(fd));
  conn->transport_active = TRUE;

  if (conn->listener->tls_context)
    {
      log_transport_stack_add_factory(&conn->stack,
                                      transport_factory_tls_new(conn->listener->tls_context, NULL));
      if (!log_transport_stack_switch(&conn->stack, LOG_TRANSPORT_TLS))
        {
          msg_error("http(): failed to set up TLS session for the connection",
                    evt_tag_int("port", conn->listener->port));
          return FALSE;
        }
    }
  return TRUE;
}

/* takes ownership of the peer GSockAddr reference */
static HTTPServerConnection *
_conn_new(HTTPServerListener *listener, gint fd, GSockAddr *peer)
{
  HTTPServerConnection *conn = g_new0(HTTPServerConnection, 1);
  conn->listener = listener;
  conn->fd = fd;
  conn->peer = peer;
  conn->url = g_string_sized_new(64);
  conn->body = g_string_sized_new(1024);
  conn->write_buf = g_string_sized_new(128);

  llhttp_init(&conn->parser, HTTP_REQUEST, &http_settings);
  conn->parser.data = conn;

  IV_TASK_INIT(&conn->free_task);
  conn->free_task.cookie = conn;
  conn->free_task.handler = _conn_free_task;

  IV_TIMER_INIT(&conn->timer);
  conn->timer.cookie = conn;
  conn->timer.handler = _conn_timeout;

  IV_FD_INIT(&conn->iv_fd);
  conn->iv_fd.fd = fd;
  conn->iv_fd.cookie = conn;
  conn->iv_fd.handler_in = _conn_io_handler;
  conn->iv_fd.handler_err = _conn_error_handler;

  if (!_conn_setup_transport(conn, fd))
    {
      _conn_destroy(conn);
      return NULL;
    }

  return conn;
}

/* --- backpressure ------------------------------------------------------ */

void
http_server_listener_suspend_connection(HTTPServerConnection *conn)
{
  if (!conn->suspended)
    {
      conn->suspended = TRUE;
      conn->listener->suspended = g_list_prepend(conn->listener->suspended, conn);
    }
  /* stop watching this connection until the window reopens; the stall is on
   * our side now, so don't hold the client to the inactivity deadline */
  iv_fd_set_handler_in(&conn->iv_fd, NULL);
  iv_fd_set_handler_out(&conn->iv_fd, NULL);
  _conn_disarm_timer(conn);
}

static void
_conn_retry(HTTPServerConnection *conn)
{
  conn->suspended = FALSE;
  conn->listener->suspended = g_list_remove(conn->listener->suspended, conn);

  if (_conn_dispatch(conn) == HTTP_SERVER_REQUEST_SUSPENDED)
    {
      http_server_listener_suspend_connection(conn);
      return;
    }

  /* the message finished: leave the paused parser ready for the next request.
   * _conn_flush sends the queued response and re-arms the read watch. */
  llhttp_resume(&conn->parser);
  _conn_flush(conn);
}

static void
_wakeup_event_handler(void *cookie)
{
  HTTPServerListener *self = (HTTPServerListener *) cookie;

  /* retry on a snapshot: _conn_retry mutates self->suspended */
  GList *snapshot = g_list_copy(self->suspended);
  for (GList *l = snapshot; l; l = l->next)
    _conn_retry((HTTPServerConnection *) l->data);
  g_list_free(snapshot);
}

void
http_server_listener_wakeup(HTTPServerListener *self)
{
  if (g_atomic_counter_get(&self->events_ready))
    iv_event_post(&self->wakeup_event);
}

/* --- accept ------------------------------------------------------------ */

static void
_listener_accept(void *cookie)
{
  HTTPServerListener *self = (HTTPServerListener *) cookie;

  for (;;)
    {
      gint fd;
      GSockAddr *peer = NULL;
      GIOStatus status = g_accept(self->listen_fd.fd, &fd, &peer);
      if (status == G_IO_STATUS_AGAIN)
        break;
      if (status != G_IO_STATUS_NORMAL)
        {
          msg_error("http(): failed to accept connection",
                    evt_tag_int("port", self->port), evt_tag_error("error"));
          break;
        }

      int flags = fcntl(fd, F_GETFL, 0);
      fcntl(fd, F_SETFL, flags | O_NONBLOCK);

      HTTPServerConnection *conn = _conn_new(self, fd, peer);
      if (!conn)
        continue;       /* TLS setup failed; the fd is already closed */
      self->connections = g_list_prepend(self->connections, conn);
      _conn_start(conn);
    }
}

/* --- listener thread --------------------------------------------------- */

static void
_stop_event_handler(void *cookie)
{
  (void) cookie;
  iv_quit();
}

static void
_destroy_all_connections(HTTPServerListener *self)
{
  GList *connections = self->connections;
  self->connections = NULL;
  g_list_free(self->suspended);
  self->suspended = NULL;

  for (GList *l = connections; l; l = l->next)
    _conn_destroy((HTTPServerConnection *) l->data);
  g_list_free(connections);
}

static void
_listener_thread_run(MainLoopThreadedWorker *t)
{
  HTTPServerListener *self = (HTTPServerListener *) t->data;

  iv_fd_register(&self->listen_fd);
  iv_event_register(&self->wakeup_event);
  iv_event_register(&self->stop_event);

  g_atomic_counter_set(&self->events_ready, 1);

  iv_main();

  g_atomic_counter_set(&self->events_ready, 0);
  iv_event_unregister(&self->wakeup_event);
  iv_event_unregister(&self->stop_event);
  iv_fd_unregister(&self->listen_fd);

  _destroy_all_connections(self);
}

static void
_listener_thread_request_exit(MainLoopThreadedWorker *t)
{
  HTTPServerListener *self = (HTTPServerListener *) t->data;
  iv_event_post(&self->stop_event);
}

gboolean
http_server_listener_start(HTTPServerListener *self)
{
  if (self->thread_started)
    return TRUE;

  self->thread_started = TRUE;
  return main_loop_threaded_worker_start(&self->thread);
}

/* --- path registration ------------------------------------------------- */

gboolean
http_server_listener_register_path(HTTPServerListener *self, const gchar *path,
                                   HTTPServerRequestHandler handler,
                                   HTTPServerRequestCompletion completion,
                                   gpointer user_data, gsize max_request_size)
{
  g_mutex_lock(&self->lock);

  if (g_hash_table_contains(self->paths, path))
    msg_warning("http(): a handler is already registered for this path on the given address, "
                "the previous registration will be replaced",
                evt_tag_str("path", path), evt_tag_int("port", self->port));

  PathRegistration *reg = g_new0(PathRegistration, 1);
  reg->handler = handler;
  reg->completion = completion;
  reg->user_data = user_data;
  reg->max_request_size = max_request_size;
  g_hash_table_insert(self->paths, g_strdup(path), reg);

  g_mutex_unlock(&self->lock);

  msg_debug("http(): URL handler registered",
            evt_tag_str("ip", self->bind_addr),
            evt_tag_int("port", self->port),
            evt_tag_str("path", path));
  return TRUE;
}

void
http_server_listener_unregister_path(HTTPServerListener *self, const gchar *path,
                                     gpointer user_data)
{
  g_mutex_lock(&self->lock);

  PathRegistration *reg = g_hash_table_lookup(self->paths, path);
  /* only remove the entry if it still belongs to us; during a reload a freshly
   * initialized registration may have already taken over this path */
  if (reg && reg->user_data == user_data)
    g_hash_table_remove(self->paths, path);

  g_mutex_unlock(&self->lock);
}

/* --- listen socket ----------------------------------------------------- */

/* create a non-blocking, bound, listening socket for the given address */
static gint
_open_socket(GSockAddr *addr, gboolean v6only_off)
{
  struct sockaddr *sa = g_sockaddr_get_sa(addr);

  gint fd = socket(sa->sa_family, SOCK_STREAM, 0);
  if (fd < 0)
    return -1;

  int on = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#ifdef SO_REUSEPORT
  setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
#endif
  if (sa->sa_family == AF_INET6)
    {
      int v6only = v6only_off ? 0 : 1;
      setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
    }

  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);

  if (g_bind(fd, addr) != G_IO_STATUS_NORMAL || listen(fd, SOMAXCONN) < 0)
    {
      close(fd);
      return -1;
    }
  return fd;
}

static gboolean
_open_listen_socket(HTTPServerListener *self)
{
  GSockAddr *addr = NULL;
  gchar buf[MAX_SOCKADDR_STRING];
  gboolean success = FALSE;

  if (!self->bind_addr || !self->bind_addr[0])
    {
      /* no bind address given: prefer a dual-stack IPv6 wildcard, fall back to
       * IPv4-only.  An explicit "0.0.0.0" or "::" is honoured as-is below. */
      addr = g_sockaddr_inet6_new("::", (guint16) self->port);
      if (addr)
        self->listen_fd.fd = _open_socket(addr, TRUE);

      if (self->listen_fd.fd < 0)
        {
          g_sockaddr_unref(addr);
          msg_debug("http(): could not open a dual-stack listener, falling back to IPv4",
                    evt_tag_int("port", self->port));
          addr = g_sockaddr_inet_new("0.0.0.0", (guint16) self->port);
          if (addr)
            self->listen_fd.fd = _open_socket(addr, FALSE);
        }
    }
  else
    {
      addr = g_sockaddr_inet_or_inet6_new(self->bind_addr, (guint16) self->port);
      if (!addr)
        {
          msg_error("http(): could not parse bind address, expected a numeric IPv4 or IPv6 address",
                    evt_tag_str("ip", self->bind_addr));
          return FALSE;
        }
      self->listen_fd.fd = _open_socket(addr, FALSE);
    }

  if (self->listen_fd.fd < 0)
    {
      msg_error("http(): failed to start HTTP listener",
                evt_tag_str("ip", self->bind_addr), evt_tag_int("port", self->port),
                evt_tag_error("error"));
    }
  else
    {
      msg_info("Accepting HTTP connections",
               evt_tag_str("addr", g_sockaddr_format(addr, buf, sizeof(buf), GSA_FULL)));
      success = TRUE;
    }

  g_sockaddr_unref(addr);
  return success;
}

static gboolean
_listener_start(HTTPServerListener *self)
{
  return _open_listen_socket(self);
}

static void
_listener_stop(HTTPServerListener *self)
{
  if (self->thread_started)
    main_loop_threaded_worker_clear(&self->thread);

  if (self->listen_fd.fd >= 0)
    {
      close(self->listen_fd.fd);
      self->listen_fd.fd = -1;
    }
}

/* --- construction / registry ------------------------------------------- */

static void
_init_settings(void)
{
  static gboolean initialized = FALSE;
  if (initialized)
    return;

  llhttp_settings_init(&http_settings);
  http_settings.on_message_begin = _on_message_begin;
  http_settings.on_url = _on_url;
  http_settings.on_headers_complete = _on_headers_complete;
  http_settings.on_body = _on_body;
  http_settings.on_message_complete = _on_message_complete;
  initialized = TRUE;
}

static void
_init_watches(HTTPServerListener *self)
{
  IV_FD_INIT(&self->listen_fd);
  self->listen_fd.fd = -1;
  self->listen_fd.cookie = self;
  self->listen_fd.handler_in = _listener_accept;

  IV_EVENT_INIT(&self->wakeup_event);
  self->wakeup_event.cookie = self;
  self->wakeup_event.handler = _wakeup_event_handler;

  IV_EVENT_INIT(&self->stop_event);
  self->stop_event.cookie = self;
  self->stop_event.handler = _stop_event_handler;
}

static HTTPServerListener *
_listener_new(const gchar *key, const gchar *bind_addr, gint port, gint connection_timeout,
              TLSContext *tls_context)
{
  HTTPServerListener *self = g_new0(HTTPServerListener, 1);

  _init_settings();

  self->user_count = 1;
  self->key = g_strdup(key);
  self->bind_addr = g_strdup(bind_addr ? bind_addr : "");
  self->port = port;
  self->connection_timeout = connection_timeout;
  self->tls_context = tls_context ? tls_context_ref(tls_context) : NULL;
  g_mutex_init(&self->lock);
  g_atomic_counter_set(&self->events_ready, 0);
  self->paths = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  _init_watches(self);

  main_loop_threaded_worker_init(&self->thread, MLW_THREADED_INPUT_WORKER, self);
  self->thread.run = _listener_thread_run;
  self->thread.request_exit = _listener_thread_request_exit;
  return self;
}

static void
_listener_free(HTTPServerListener *self)
{
  g_assert(self->listen_fd.fd == -1);
  g_hash_table_unref(self->paths);
  g_mutex_clear(&self->lock);
  if (self->tls_context)
    tls_context_unref(self->tls_context);
  g_free(self->key);
  g_free(self->bind_addr);
  g_free(self);
}

/* GDestroyNotify for the registry */
static void
_registry_destroy_listener(gpointer p)
{
  HTTPServerListener *self = (HTTPServerListener *) p;
  _listener_stop(self);
  _listener_free(self);
}

static GHashTable *
_get_registry(GlobalConfig *cfg)
{
  HTTPSourceConfig *hsc = http_source_config_get(cfg);
  if (!hsc->listeners)
    hsc->listeners = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, _registry_destroy_listener);
  return hsc->listeners;
}

HTTPServerListener *
http_server_listener_acquire(GlobalConfig *cfg, const gchar *bind_addr, gint port, gint connection_timeout,
                             TLSContext *tls_context)
{
  gchar key[128];
  GHashTable *registry = _get_registry(cfg);

  g_snprintf(key, sizeof(key), "%s:%d", bind_addr ? bind_addr : "", port);

  HTTPServerListener *listener = g_hash_table_lookup(registry, key);
  if (listener)
    {
      if (listener->connection_timeout != connection_timeout)
        msg_warning("http(): sources sharing a port disagree on timeout(), keeping the first value",
                    evt_tag_int("port", port),
                    evt_tag_int("timeout", listener->connection_timeout));
      if ((listener->tls_context != NULL) != (tls_context != NULL) || listener->tls_context != tls_context)
        msg_warning("http(): sources sharing a port disagree on tls(), keeping the first source's setting",
                    evt_tag_int("port", port));
      listener->user_count++;
      return listener;
    }

  listener = _listener_new(key, bind_addr, port, connection_timeout, tls_context);
  if (_listener_start(listener))
    g_hash_table_insert(registry, listener->key, listener);
  else
    {
      _listener_free(listener);
      listener = NULL;
    }

  return listener;
}

void
http_server_listener_release(GlobalConfig *cfg, HTTPServerListener *listener)
{
  GHashTable *registry = _get_registry(cfg);
  /* the last reference removes the listener from the registry, which destroys
   * it (stop + free) via the hash table's value-destroy function */
  if (--listener->user_count == 0)
    g_hash_table_remove(registry, listener->key);
}
