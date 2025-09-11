/*
 * Copyright (c) 2022 One Identity LLC.
 * Copyright (c) 2018 Balazs Scheidler
 * Copyright (c) 2016 Marc Falzon
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

#ifndef HTTP_H_INCLUDED
#define HTTP_H_INCLUDED 1

#define HTTP_DEFAULT_URL "http://localhost/"
#define METHOD_TYPE_POST 1
#define METHOD_TYPE_PUT  2

#include "logthrdest/logthrdestdrv.h"
#include "http-loadbalancer.h"
#include "response-handler.h"

typedef struct
{
  LogThreadedDestDriver super;
  HTTPLoadBalancer *load_balancer;

  /* this is the first URL in load-balanced configurations and serves as the
   * identifier in persist/stats. TODO: Templated URLs should have dynamic counters. */
  const gchar *url;
  gchar *user;
  gchar *password;
  GList *headers;
  gchar *user_agent;
  gchar *ca_dir;
  gchar *ca_file;
  gchar *cert_file;
  gchar *key_file;
  gchar *ciphers;
  gchar *tls13_ciphers;
  gchar *proxy;
  LogTemplate *body_prefix_template;
  GString *body_suffix;
  GString *delimiter;
  long ssl_version;
  GString *accept_encoding;
  gint8 content_compression;
  gboolean peer_verify;
  gboolean ocsp_stapling_verify;
  gboolean accept_redirects;
  short int method_type;
  glong timeout;
  glong batch_bytes;
  LogTemplate *body_template;
  LogTemplateOptions template_options;
  HttpResponseHandlers *response_handlers;
} HTTPDestinationDriver;

gboolean http_dd_init(LogPipe *s);
gboolean http_dd_deinit(LogPipe *s);
LogDriver *http_dd_new(GlobalConfig *cfg);

gboolean http_dd_set_urls(LogDriver *d, GList *urls, GError **error);
void http_dd_set_user(LogDriver *d, const gchar *user);
void http_dd_set_password(LogDriver *d, const gchar *password);
void http_dd_set_method(LogDriver *d, const gchar *method);
void http_dd_set_user_agent(LogDriver *d, const gchar *user_agent);
void http_dd_set_headers_ref(LogDriver *d, GList *headers);
void http_dd_set_body(LogDriver *d, LogTemplate *body);
void http_dd_set_accept_redirects(LogDriver *d, gboolean accept_redirects);
void http_dd_set_ca_dir(LogDriver *d, const gchar *ca_dir);
void http_dd_set_ca_file(LogDriver *d, const gchar *ca_file);
void http_dd_set_cert_file(LogDriver *d, const gchar *cert_file);
void http_dd_set_key_file(LogDriver *d, const gchar *key_file);
void http_dd_set_cipher_suite(LogDriver *d, const gchar *ciphers);
gboolean http_dd_set_tls13_cipher_suite(LogDriver *d, const gchar *tls13_ciphers);
void http_dd_set_proxy(LogDriver *d, const gchar *proxy);
gboolean http_dd_set_ssl_version(LogDriver *d, const gchar *value);
void http_dd_set_peer_verify(LogDriver *d, gboolean verify);
gboolean http_dd_set_ocsp_stapling_verify(LogDriver *d, gboolean verify);
void http_dd_set_timeout(LogDriver *d, glong timeout);
void http_dd_set_batch_bytes(LogDriver *d, glong batch_bytes);
void http_dd_set_body_prefix(LogDriver *d, LogTemplate *body_prefix);
void http_dd_set_body_suffix(LogDriver *d, const gchar *body_suffix);
void http_dd_set_delimiter(LogDriver *d, const gchar *delimiter);
void http_dd_insert_response_handler(LogDriver *d, HttpResponseHandler *response_handler);
LogTemplateOptions *http_dd_get_template_options(LogDriver *d);
void http_dd_set_accept_encoding(LogDriver *d, const gchar *encoding);
gboolean http_dd_set_content_compression(LogDriver *d, const gchar *encoding);

#endif
