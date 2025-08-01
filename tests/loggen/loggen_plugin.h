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

#ifndef LOGGEN_PLUGIN_H_INCLUDED
#define LOGGEN_PLUGIN_H_INCLUDED

#include "compat/glib.h"
#include "atomic-gssize.h"
#include <gmodule.h>
#include <sys/time.h>

#define LOGGEN_PLUGIN_INFO "loggen_plugin_info"
#define LOGGEN_PLUGIN_LIB_PREFIX "libloggen_"
#define LOGGEN_PLUGIN_NAME_MAXSIZE 100

typedef struct _plugin_option
{
  int message_length;
  int interval;
  int number_of_messages;
  int permanent;
  int perf;
  int active_connections;
  int idle_connections;
  int use_ipv6;
  char *target; /* command line argument */
  char *port;
  gint client_port;
  gint64 rate;
  int rate_burst_start;
  int reconnect;
  gboolean proxied;
  gint proxy_version;
  char *proxy_src_ip;
  char *proxy_dst_ip;
  char *proxy_src_port;
  char *proxy_dst_port;

  atomic_gssize global_sent_messages;
  guint64 global_sent_bytes;
} PluginOption;

typedef struct _thread_data
{
  PluginOption *option;
  int index;
  gsize sent_messages;
  guint64 sent_bytes;
  struct timeval start_time;
  struct timespec last_throttle_check;
  gint64 buckets;
  gdouble bucket_remainder;
  gboolean proxy_header_sent;

  /* timestamp  cache for logline generator */
  struct timeval ts_formatted;
  char stamp[32];
} ThreadData;

typedef GOptionEntry *(*get_option_func)(void);
typedef gboolean (*start_plugin_func)(PluginOption *option);
typedef void (*stop_plugin_func)(PluginOption *option);
typedef gboolean (*wait_with_timeout_func)(PluginOption *option, gint timeout_usec);
typedef int (*generate_message_func)(char *buffer, int buffer_size, ThreadData *thread_context, gsize seq);
typedef void (*set_generate_message_func)(generate_message_func gen_message);
typedef int (*get_thread_count_func)(void);
typedef gboolean (*is_plugin_activated_func)(void);

typedef struct _plugin_info
{
  gchar              *name;
  get_option_func    get_options_list;
  get_thread_count_func  get_thread_count;
  stop_plugin_func   stop_plugin;
  start_plugin_func  start_plugin;
  wait_with_timeout_func wait_with_timeout;
  set_generate_message_func set_generate_message;
  gboolean  require_framing; /* plugin can indicates that framing is mandatory in message lines */
  is_plugin_activated_func is_plugin_activated;
} PluginInfo;

gboolean thread_check_exit_criteria(ThreadData *thread_context);
gboolean thread_check_time_bucket(ThreadData *thread_context);

#endif
