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

#include "http-source.h"
#include "http-server-listener.h"
#include "logsource.h"
#include "mainloop-worker.h"
#include "messages.h"
#include "stats/stats-cluster-key-builder.h"
#include "logmsg/logmsg.h"
#include "cfg.h"

#define DEFAULT_MAX_REQUEST_SIZE (8 * 1024 * 1024)
#define DEFAULT_TIMEOUT 30

/*
 * A minimal LogSource: the listener thread (a registered mainloop worker
 * thread) posts messages into it directly via log_source_post().  We only
 * override wakeup(), which the flow-control/ack path calls when the window
 * reopens, to wake the listener so it retries the connections it suspended
 * for this source.  The listener owns the suspended-connection bookkeeping
 * (it owns the connections); the source just signals it.
 */
typedef struct _HTTPSource
{
  LogSource super;
  HTTPServerListener *listener;
} HTTPSource;

struct HTTPSourceDriver
{
  LogSrcDriver super;

  gint port;
  gchar *bind_addr;
  gchar *path;
  gsize max_request_size;
  gint timeout;

  LogSourceOptions source_options;
  HTTPServerListener *listener;
  HTTPSource *source;
};

/* LogSource wakeup: the window reopened, ask the listener to retry suspended
 * connections (runs on the ack path, possibly off the listener thread) */
static void
_source_wakeup(LogSource *s)
{
  HTTPSource *self = (HTTPSource *) s;
  if (self->listener)
    http_server_listener_wakeup(self->listener);
}

/*
 * HTTPServerRequestHandler: runs on the listener thread.  Posts the body line
 * by line; on a full window the connection is suspended and retried later.
 *
 * No locking is needed around the window check: this handler and the listener's
 * retry-on-wakeup both run on the listener thread, so they never interleave.  A
 * window reopen arriving from another thread is delivered as an event processed
 * after this handler returns, so a suspended connection is always retried.
 */
static HTTPServerRequestResult
_handle_request_body(gpointer user_data, HTTPServerConnection *connection,
                     const gchar *body, gsize body_len, gsize *offset, GSockAddr *peer)
{
  HTTPSource *self = (HTTPSource *) user_data;

  while (*offset < body_len)
    {
      const gchar *p = body + *offset;
      const gchar *nl = memchr(p, '\n', body_len - *offset);
      gsize seg_len = nl ? (gsize) (nl - p) : (body_len - *offset);
      gsize advance = nl ? seg_len + 1 : seg_len;

      gsize line_len = seg_len;
      if (line_len > 0 && p[line_len - 1] == '\r')
        line_len--;

      if (line_len == 0)
        {
          *offset += advance;
          continue;
        }

      if (!log_source_free_to_send(&self->super))
        {
          http_server_listener_suspend_connection(connection);

          /* flush what we have already posted so the destination can drain
           * it and reopen the window (otherwise the wakeup never comes) */
          main_loop_worker_invoke_batch_callbacks();
          return HTTP_SERVER_REQUEST_SUSPENDED;
        }

      LogMessage *msg = log_msg_new_empty();
      log_msg_set_value(msg, LM_V_MESSAGE, p, line_len);
      log_msg_set_value(msg, LM_V_TRANSPORT, "http", 4);
      if (peer)
        log_msg_set_saddr_ref(msg, g_sockaddr_ref(peer));
      log_source_post(&self->super, msg);

      *offset += advance;
    }

  main_loop_worker_invoke_batch_callbacks();
  return HTTP_SERVER_REQUEST_COMPLETE;
}

static HTTPSource *
_http_source_new(GlobalConfig *cfg)
{
  HTTPSource *self = g_new0(HTTPSource, 1);
  log_source_init_instance(&self->super, cfg);
  self->super.wakeup = _source_wakeup;
  return self;
}


static void
_format_stats_kb(HTTPSourceDriver *self, StatsClusterKeyBuilder *kb)
{
  stats_cluster_key_builder_add_legacy_label(kb, stats_cluster_label("driver", "http"));

  gchar buf[64];
  g_snprintf(buf, sizeof(buf), "%d", self->port);
  stats_cluster_key_builder_add_legacy_label(kb, stats_cluster_label("port", buf));

  if (self->path)
    stats_cluster_key_builder_add_legacy_label(kb, stats_cluster_label("path", self->path));
}

static gboolean
_pre_config_init(LogPipe *s)
{
  /* reserve a thread slot for the (shared) listener thread; over-allocating
   * when several sources share a listener is harmless */
  main_loop_worker_allocate_thread_space(1);
  return TRUE;
}

static gboolean
_post_config_init(LogPipe *s)
{
  HTTPSourceDriver *self = (HTTPSourceDriver *) s;

  if (!self->listener)
    return FALSE;

  return http_server_listener_start(self->listener);
}

static gboolean
_init(LogPipe *s)
{
  HTTPSourceDriver *self = (HTTPSourceDriver *) s;
  GlobalConfig *cfg = log_pipe_get_config(s);

  if (self->port <= 0 || self->port > 65535)
    {
      msg_error("http(): the port() option is mandatory and must be between 1 and 65535",
                log_pipe_location_tag(s));
      return FALSE;
    }

  if (!self->path)
    {
      msg_error("http(): the path() option is mandatory", log_pipe_location_tag(s));
      return FALSE;
    }

  if (!log_src_driver_init_method(s))
    return FALSE;

  log_source_options_init(&self->source_options, cfg, self->super.super.group);

  self->source = _http_source_new(cfg);

  StatsClusterKeyBuilder *kb = stats_cluster_key_builder_new();
  _format_stats_kb(self, kb);
  log_source_set_options(&self->source->super, &self->source_options, self->super.super.id, kb, TRUE,
                         self->super.super.super.expr_node);

  log_pipe_append(&self->source->super.super, &self->super.super.super);
  if (!log_pipe_init(&self->source->super.super))
    return FALSE;

  self->listener = http_server_listener_acquire(cfg, self->bind_addr, self->port, self->timeout);
  if (!self->listener)
    return FALSE;

  /* the source wakes the listener (to retry suspended connections) through the
   * listener it is registered on */
  self->source->listener = self->listener;

  http_server_listener_register_path(self->listener, self->path,
                                     _handle_request_body, NULL, self->source,
                                     self->max_request_size);
  return TRUE;
}

static gboolean
_deinit(LogPipe *s)
{
  HTTPSourceDriver *self = (HTTPSourceDriver *) s;
  GlobalConfig *cfg = log_pipe_get_config(s);

  if (self->listener)
    http_server_listener_unregister_path(self->listener, self->path, self->source);

  if (self->source)
    {
      log_pipe_deinit(&self->source->super.super);
      log_pipe_unref(&self->source->super.super);
      self->source = NULL;
    }

  if (self->listener)
    {
      http_server_listener_release(cfg, self->listener);
      self->listener = NULL;
    }

  return log_src_driver_deinit_method(s);
}

static void
_free(LogPipe *s)
{
  HTTPSourceDriver *self = (HTTPSourceDriver *) s;

  if (self->source)
    {
      log_pipe_unref(&self->source->super.super);
      self->source = NULL;
    }

  g_free(self->bind_addr);
  g_free(self->path);
  log_source_options_destroy(&self->source_options);

  log_src_driver_free(s);
}

LogSourceOptions *
http_sd_get_source_options(LogDriver *s)
{
  HTTPSourceDriver *self = (HTTPSourceDriver *) s;
  return &self->source_options;
}

void
http_sd_set_port(LogDriver *s, gint port)
{
  HTTPSourceDriver *self = (HTTPSourceDriver *) s;
  self->port = port;
}

void
http_sd_set_bind_addr(LogDriver *s, const gchar *addr)
{
  HTTPSourceDriver *self = (HTTPSourceDriver *) s;
  g_free(self->bind_addr);
  self->bind_addr = g_strdup(addr);
}

void
http_sd_set_path(LogDriver *s, const gchar *path)
{
  HTTPSourceDriver *self = (HTTPSourceDriver *) s;
  g_free(self->path);
  self->path = g_strdup(path);
}

void
http_sd_set_max_request_size(LogDriver *s, gsize size)
{
  HTTPSourceDriver *self = (HTTPSourceDriver *) s;
  self->max_request_size = size;
}

void
http_sd_set_timeout(LogDriver *s, gint timeout)
{
  HTTPSourceDriver *self = (HTTPSourceDriver *) s;
  self->timeout = timeout;
}

LogDriver *
http_sd_new(GlobalConfig *cfg)
{
  HTTPSourceDriver *self = g_new0(HTTPSourceDriver, 1);
  log_src_driver_init_instance(&self->super, cfg);

  /* bind_addr stays NULL by default: bind to a dual-stack wildcard */
  self->max_request_size = DEFAULT_MAX_REQUEST_SIZE;
  self->timeout = DEFAULT_TIMEOUT;
  log_source_options_defaults(&self->source_options);

  self->super.super.super.init = _init;
  self->super.super.super.deinit = _deinit;
  self->super.super.super.free_fn = _free;
  self->super.super.super.pre_config_init = _pre_config_init;
  self->super.super.super.post_config_init = _post_config_init;

  return &self->super.super;
}
