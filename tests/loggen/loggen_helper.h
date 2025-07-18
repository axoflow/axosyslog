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

#ifndef LOGGEN_HELPER_H_INCLUDED
#define LOGGEN_HELPER_H_INCLUDED

#include "compat/compat.h"
#include "compat/glib.h"

#include <stddef.h>
#include <stdio.h>
#include <glib.h>
#include <sys/un.h>
#include <netdb.h>
#include <openssl/ssl.h>
#include <time.h>

#include <libgen.h>

#define MAX_MESSAGE_LENGTH 8192
#define USEC_PER_SEC      1000000
#define CONNECTION_TIMEOUT_SEC 5
#define PERIODIC_STAT_USEC (500 * 1000)

int get_debug_level(void);
void set_debug_level(int new_debug);
gint64 time_spec_diff_in_usec(struct timespec *t1, struct timespec *t2);
gint64 time_val_diff_in_usec(struct timeval *t1, struct timeval *t2);
gint64 time_val_diff_in_msec(struct timeval *t1, struct timeval *t2);
double time_val_diff_in_sec(struct timeval *t1, struct timeval *t2);
void time_val_diff_in_timeval(struct timeval *res, const struct timeval *t1, const struct timeval *t2);
size_t get_now_timestamp(char *stamp, gsize stamp_size);
size_t get_now_timestamp_bsd(char *stamp, gsize stamp_size);
void format_timezone_offset_with_colon(char *timestamp, int timestamp_size, struct tm *tm);
int connect_ip_socket(int sock_type, const char *target, const char *port, int use_ipv6, int client_port);
int connect_unix_domain_socket(int sock_type, const char *path);
SSL *open_ssl_connection(int sock_fd);
void close_ssl_connection(SSL *ssl);
int generate_proxy_header(char *buffer, int buffer_size, int thread_id, int proxy_version, const char *proxy_src_ip,
                          const char *proxy_dst_ip, const char *proxy_src_port, const char *proxy_dst_port);

static inline gint
fast_gettime(struct timespec *t)
{
#ifdef CLOCK_MONOTONIC_COARSE
  if (clock_gettime(CLOCK_MONOTONIC_COARSE, t) == 0)
    return 0;
#endif

  return clock_gettime(CLOCK_MONOTONIC, t);
}

#define ERROR(format,...) do {\
  gchar *base = g_path_get_basename(__FILE__);\
  fprintf(stderr, "error [%s:%s:%d] ", base, __func__, __LINE__);\
  fprintf(stderr, format, ##__VA_ARGS__);\
  g_free(base);\
} while (0)

/* debug messages can be turned on by "--debug" command line option */
#define DEBUG(format,...) do {\
  if (!get_debug_level()) \
    break; \
  gchar *base = g_path_get_basename(__FILE__);\
  fprintf(stdout, "debug [%s:%s:%d] ", base, __func__, __LINE__); \
  fprintf(stdout, format, ##__VA_ARGS__);\
  g_free(base);\
} while(0)

#endif
