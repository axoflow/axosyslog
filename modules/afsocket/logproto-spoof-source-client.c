/*
 * Copyright (c) 2002-2012 Balabit
 * Copyright (c) 1998-2012 Balázs Scheidler
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

#include "logproto-spoof-source-client.h"
#include "logproto/logproto-text-client.h"
#include "logmsg/logmsg.h"
#include "messages.h"
#include "gprocess.h"

#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

#ifdef _GNU_SOURCE
#  define _GNU_SOURCE_DEFINED 1
#  undef _GNU_SOURCE
#endif

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wundef"
#include <libnet.h>
#pragma GCC diagnostic pop

#ifdef _GNU_SOURCE_DEFINED
#  undef _GNU_SOURCE
#  define _GNU_SOURCE 1
#endif

typedef struct _LogProtoSpoofSourceClient
{
  LogProtoTextClient super;
  libnet_t *lnet_ctx;
  GSockAddr *dest_addr;
  guint max_msglen;
} LogProtoSpoofSourceClient;

static inline gboolean
_is_message_spoofable(LogMessage *msg)
{
  return msg->saddr && (msg->saddr->sa.sa_family == AF_INET || msg->saddr->sa.sa_family == AF_INET6);
}

static gboolean
_construct_ipv4_packet(LogProtoSpoofSourceClient *self, LogMessage *msg, const guchar *payload, gsize payload_len)
{
  libnet_ptag_t ip, udp;
  struct sockaddr_in *src, *dst;

  if (msg->saddr->sa.sa_family != AF_INET)
    return FALSE;

  src = (struct sockaddr_in *) &msg->saddr->sa;
  dst = (struct sockaddr_in *) &self->dest_addr->sa;

  libnet_clear_packet(self->lnet_ctx);

  udp = libnet_build_udp(ntohs(src->sin_port),
                         ntohs(dst->sin_port),
                         LIBNET_UDP_H + payload_len,
                         0,
                         (guchar *) payload,
                         payload_len,
                         self->lnet_ctx,
                         0);
  if (udp == -1)
    return FALSE;

  ip = libnet_build_ipv4(LIBNET_IPV4_H + payload_len + LIBNET_UDP_H,
                         IPTOS_LOWDELAY,
                         0,
                         0,
                         64,
                         IPPROTO_UDP,
                         0,
                         src->sin_addr.s_addr,
                         dst->sin_addr.s_addr,
                         NULL,
                         0,
                         self->lnet_ctx,
                         0);
  return ip != -1;
}

#if SYSLOG_NG_ENABLE_IPV6
static gboolean
_construct_ipv6_packet(LogProtoSpoofSourceClient *self, LogMessage *msg, const guchar *payload, gsize payload_len)
{
  libnet_ptag_t ip, udp;
  struct sockaddr_in *src4;
  struct sockaddr_in6 src, *dst;
  struct libnet_in6_addr ln_src, ln_dst;

  switch (msg->saddr->sa.sa_family)
    {
    case AF_INET:
      src4 = (struct sockaddr_in *) &msg->saddr->sa;
      memset(&src, 0, sizeof(src));
      src.sin6_family = AF_INET6;
      src.sin6_port = src4->sin_port;
      ((guint32 *) &src.sin6_addr)[0] = 0;
      ((guint32 *) &src.sin6_addr)[1] = 0;
      ((guint32 *) &src.sin6_addr)[2] = htonl(0xffff);
      ((guint32 *) &src.sin6_addr)[3] = src4->sin_addr.s_addr;
      break;
    case AF_INET6:
      src = *((struct sockaddr_in6 *) &msg->saddr->sa);
      break;
    default:
      g_assert_not_reached();
      break;
    }

  dst = (struct sockaddr_in6 *) &self->dest_addr->sa;

  libnet_clear_packet(self->lnet_ctx);

  udp = libnet_build_udp(ntohs(src.sin6_port),
                         ntohs(dst->sin6_port),
                         LIBNET_UDP_H + payload_len,
                         0,
                         (guchar *) payload,
                         payload_len,
                         self->lnet_ctx,
                         0);
  if (udp == -1)
    return FALSE;

  memcpy(&ln_src, &src.sin6_addr, sizeof(ln_src));
  memcpy(&ln_dst, &dst->sin6_addr, sizeof(ln_dst));
  ip = libnet_build_ipv6(0, 0,
                         LIBNET_UDP_H + payload_len,
                         IPPROTO_UDP,
                         64,
                         ln_src, ln_dst,
                         NULL, 0,
                         self->lnet_ctx,
                         0);
  return ip != -1;
}
#endif

static gboolean
_construct_packet(LogProtoSpoofSourceClient *self, LogMessage *msg, const guchar *payload, gsize payload_len)
{
  switch (self->dest_addr->sa.sa_family)
    {
    case AF_INET:
      return _construct_ipv4_packet(self, msg, payload, payload_len);
#if SYSLOG_NG_ENABLE_IPV6
    case AF_INET6:
      return _construct_ipv6_packet(self, msg, payload, payload_len);
#endif
    default:
      g_assert_not_reached();
    }
  return FALSE;
}

static gboolean
log_proto_spoof_source_client_poll_prepare(LogProtoClient *s, GIOCondition *cond, GIOCondition *idle_cond,
                                           gint *timeout)
{
  LogProtoSpoofSourceClient *self = (LogProtoSpoofSourceClient *) s;

  *cond = G_IO_OUT;
  *idle_cond = 0;
  return self->super.partial != NULL;
}

static LogProtoStatus
log_proto_spoof_source_client_post(LogProtoClient *s, LogMessage *logmsg, guchar *msg, gsize msg_len,
                                   gboolean *consumed)
{
  LogProtoSpoofSourceClient *self = (LogProtoSpoofSourceClient *) s;

  if (!_is_message_spoofable(logmsg))
    return log_proto_text_client_post_method(s, logmsg, msg, msg_len, consumed);

  *consumed = FALSE;

  gsize effective_len = MIN(msg_len, (gsize) self->max_msglen);
  gboolean success = _construct_packet(self, logmsg, msg, effective_len)
                     && (libnet_write(self->lnet_ctx) >= 0);

  if (!success)
    {
      msg_error("Error sending raw frame, falling back to normal UDP",
                evt_tag_str("error", libnet_geterror(self->lnet_ctx)));
      return log_proto_text_client_post_method(s, logmsg, msg, msg_len, consumed);
    }

  *consumed = TRUE;
  log_proto_client_msg_ack(s, 1);
  return LPS_SUCCESS;
}

static void
log_proto_spoof_source_client_free(LogProtoClient *s)
{
  LogProtoSpoofSourceClient *self = (LogProtoSpoofSourceClient *) s;

  if (self->lnet_ctx)
    libnet_destroy(self->lnet_ctx);
  if (self->dest_addr)
    g_sockaddr_unref(self->dest_addr);
  if (self->super.partial_free)
    self->super.partial_free(self->super.partial);
  self->super.partial = NULL;
  log_proto_client_free_method(s);
}

LogProtoClient *
log_proto_spoof_source_client_new(gint address_family,
                                   const LogProtoClientOptions *options,
                                   GSockAddr *dest_addr,
                                   guint max_msglen)
{
  LogProtoSpoofSourceClient *self = g_new0(LogProtoSpoofSourceClient, 1);

  log_proto_text_client_init(&self->super, NULL, options);
  self->super.super.poll_prepare = log_proto_spoof_source_client_poll_prepare;
  self->super.super.post = log_proto_spoof_source_client_post;
  self->super.super.free_fn = log_proto_spoof_source_client_free;

  gchar error[LIBNET_ERRBUF_SIZE];
  cap_t saved_caps = g_process_cap_save();
  g_process_enable_cap("cap_net_raw");
  self->lnet_ctx = libnet_init(address_family == AF_INET ? LIBNET_RAW4 : LIBNET_RAW6, NULL, error);
  g_process_cap_restore(saved_caps);

  if (!self->lnet_ctx)
    {
      msg_error("Error initializing raw socket, spoof-source support disabled",
                evt_tag_str("error", error));
      log_proto_client_free(&self->super.super);
      return NULL;
    }

  self->dest_addr = g_sockaddr_ref(dest_addr);
  self->max_msglen = max_msglen;

  return &self->super.super;
}
