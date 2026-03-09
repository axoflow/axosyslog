/*
 * Copyright (c) 2020-2023 One Identity LLC.
 * Copyright (c) 2022 Balazs Scheidler <bazsi77@gmail.com>
 * Copyright (c) 2024 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#include "transport/transport-haproxy.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "syslog-ng.h"
#include "messages.h"
#include "str-utils.h"

#define IP_BUF_SIZE 64

/* This class implements: https://www.haproxy.org/download/1.8/doc/proxy-protocol.txt */

/* the size of the buffer we use to fetch the PROXY header into */
#define PROXY_PROTO_HDR_BUFFER_SIZE 1500

typedef struct _LogTransportHAProxy LogTransportHAProxy;
struct _LogTransportHAProxy
{
  LogTransportAdapter super;
  LogTransportIndex switch_to;

  /* Info received from the proxy that should be added as LogTransportAuxData to
   * any message received through this server instance. */
  struct
  {
    gboolean unknown;

    gchar src_ip[IP_BUF_SIZE];
    gchar dst_ip[IP_BUF_SIZE];

    int ip_version;
    int src_port;
    int dst_port;
  } info;

  /* Flag to only process proxy header once */
  gboolean proxy_header_processed;

  enum
  {
    LPPTS_INITIAL,
    LPPTS_DETERMINE_VERSION,
    LPPTS_PROXY_V1_READ_LINE,
    LPPTS_PROXY_V2_READ_HEADER,
    LPPTS_PROXY_V2_READ_PAYLOAD,
    LPPTS_PROXY_V2_READ_PACKET,
  } header_fetch_state;

  /* 0 unknown, 1 or 2 indicate proxy header version */
  gint proxy_header_version;
  guchar proxy_header_buff[PROXY_PROTO_HDR_BUFFER_SIZE];
  gsize proxy_header_buff_len;
  struct
  {
    struct proxy_hdr_v2 *header;
    union proxy_addr *addr;
    struct proxy_tlv *tlvs;
    gint header_len, addr_len, tlvs_len;
  } proto_v2;
};



/* This class implements: https://www.haproxy.org/download/1.8/doc/proxy-protocol.txt */

/* PROXYv1 line without newlines or terminating zero character.  The
 * protocol specification contains the number 108 that includes both the
 * CRLF sequence and the NUL */
#define PROXY_PROTO_HDR_MAX_LEN_RFC 105

/* the amount of bytes we need from the client to detect protocol version */
#define PROXY_PROTO_HDR_MAGIC_LEN   5

typedef enum
{
  STATUS_SUCCESS,
  STATUS_ERROR,
  STATUS_EOF,
  STATUS_AGAIN,
} Status;

struct proxy_hdr_v2
{
  uint8_t sig[12];  /* hex 0D 0A 0D 0A 00 0D 0A 51 55 49 54 0A */
  uint8_t ver_cmd;  /* protocol version and command */
  uint8_t fam;      /* protocol family and address */
  uint16_t len;     /* number of following bytes part of the header */
};

union proxy_addr
{
  struct
  {
    /* for TCP/UDP over IPv4, len = 12 */
    uint32_t src_addr;
    uint32_t dst_addr;
    uint16_t src_port;
    uint16_t dst_port;
  } ipv4_addr;
  struct
  {
    /* for TCP/UDP over IPv6, len = 36 */
    uint8_t  src_addr[16];
    uint8_t  dst_addr[16];
    uint16_t src_port;
    uint16_t dst_port;
  } ipv6_addr;
  struct
  {
    /* zero bytes */
  } unspec_addr;
  struct
  {
    /* for AF_UNIX sockets, len = 216 */
    uint8_t src_addr[108];
    uint8_t dst_addr[108];
  } unix_addr;
};

#define PP2_TYPE_ALPN           0x01
#define PP2_TYPE_AUTHORITY      0x02
#define PP2_TYPE_CRC32C         0x03
#define PP2_TYPE_NOOP           0x04
#define PP2_TYPE_UNIQUE_ID      0x05
#define PP2_TYPE_SSL            0x20
#define PP2_SUBTYPE_SSL_VERSION 0x21
#define PP2_SUBTYPE_SSL_CN      0x22
#define PP2_SUBTYPE_SSL_CIPHER  0x23
#define PP2_SUBTYPE_SSL_SIG_ALG 0x24
#define PP2_SUBTYPE_SSL_KEY_ALG 0x25
#define PP2_TYPE_NETNS          0x30

struct proxy_tlv
{
  uint8_t type;
  uint8_t length_hi;
  uint8_t length_lo;
  uint8_t value[0];
};

#define PROXY_HDR_TCP4 "PROXY TCP4 "
#define PROXY_HDR_TCP6 "PROXY TCP6 "
#define PROXY_HDR_UNKNOWN "PROXY UNKNOWN"

static gboolean
_check_proxy_v1_header_length(const guchar *msg, gsize msg_len)
{
  if (msg_len > PROXY_PROTO_HDR_MAX_LEN_RFC)
    {
      msg_error("PROXY proto header length exceeds max length defined by the specification",
                evt_tag_long("length", msg_len),
                evt_tag_str("header", (const gchar *)msg));
      return FALSE;
    }

  return TRUE;
}

static gboolean
_check_proxy_v1_header(const guchar *msg, gsize msg_len, const gchar *expected_header, gsize *header_len)
{
  gsize expected_header_length = strlen(expected_header);

  if (msg_len < expected_header_length)
    return FALSE;

  *header_len = expected_header_length;
  return strncmp((const gchar *)msg, expected_header, expected_header_length) == 0;
}

static gboolean
_is_proxy_v1_proto_tcp4(const guchar *msg, gsize msg_len, gsize *header_len)
{
  return _check_proxy_v1_header(msg, msg_len, PROXY_HDR_TCP4, header_len);
}

static gboolean
_is_proxy_v1_proto_tcp6(const guchar *msg, gsize msg_len, gsize *header_len)
{
  return _check_proxy_v1_header(msg, msg_len, PROXY_HDR_TCP6, header_len);
}

static gboolean
_is_proxy_v1_unknown(const guchar *msg, gsize msg_len, gsize *header_len)
{
  return _check_proxy_v1_header(msg, msg_len, PROXY_HDR_UNKNOWN, header_len);
}

static gboolean
_parse_proxy_v1_unknown_header(LogTransportHAProxy *self, const guchar *msg, gsize msg_len)
{
  if (msg_len == 0)
    return TRUE;

  msg_warning("PROXY UNKNOWN header contains unexpected parameters",
              evt_tag_mem("parameters", msg, msg_len));

  return TRUE;
}

static gboolean
_parse_proxy_v1_tcp_header(LogTransportHAProxy *self, const guchar *msg, gsize msg_len)
{
  if (msg_len == 0)
    return FALSE;

  GString *params_str = g_string_new_len((const gchar *)msg, msg_len);
  gboolean result = FALSE;
  msg_debug("PROXY header params", evt_tag_str("params", (const gchar *)msg));

  gchar **params = strsplit(params_str->str, ' ', 5);
  gint params_n = g_strv_length(params);
  if (params_n < 4)
    goto ret;

  g_strlcpy(self->info.src_ip, params[0], IP_BUF_SIZE);
  g_strlcpy(self->info.dst_ip, params[1], IP_BUF_SIZE);

  self->info.src_port = atoi(params[2]);
  if (self->info.src_port > 65535 || self->info.src_port < 0)
    msg_warning("PROXT TCP header contains invalid src port", evt_tag_str("src port", params[2]));

  self->info.dst_port = atoi(params[3]);
  if (self->info.dst_port > 65535 || self->info.dst_port < 0)
    msg_warning("PROXT TCP header contains invalid dst port", evt_tag_str("dst port", params[2]));

  if (params_n > 4)
    msg_warning("PROXY TCP header contains unexpected paramaters", evt_tag_str("parameters", params[4]));

  result = TRUE;
ret:
  if (params)
    g_strfreev(params);
  g_string_free(params_str, TRUE);

  return result;
}

static gboolean
_extract_proxy_v1_header(LogTransportHAProxy *self, guchar **msg, gsize *msg_len)
{
  if (self->proxy_header_buff[self->proxy_header_buff_len - 1] == '\n')
    self->proxy_header_buff_len--;

  if (self->proxy_header_buff[self->proxy_header_buff_len - 1] == '\r')
    self->proxy_header_buff_len--;

  self->proxy_header_buff[self->proxy_header_buff_len] = 0;
  *msg = self->proxy_header_buff;
  *msg_len = self->proxy_header_buff_len;
  return TRUE;
}

static gboolean
_parse_proxy_v1_header(LogTransportHAProxy *self)
{
  guchar *proxy_line;
  gsize proxy_line_len;

  if (!_extract_proxy_v1_header(self, &proxy_line, &proxy_line_len))
    return FALSE;


  gsize header_len = 0;

  if (!_check_proxy_v1_header_length(proxy_line, proxy_line_len))
    return FALSE;

  if (_is_proxy_v1_unknown(proxy_line, proxy_line_len, &header_len))
    {
      self->info.unknown = TRUE;
      return _parse_proxy_v1_unknown_header(self, proxy_line + header_len, proxy_line_len - header_len);
    }

  if (_is_proxy_v1_proto_tcp4(proxy_line, proxy_line_len, &header_len))
    {
      self->info.ip_version = 4;
      return _parse_proxy_v1_tcp_header(self, proxy_line + header_len, proxy_line_len - header_len);
    }

  if (_is_proxy_v1_proto_tcp6(proxy_line, proxy_line_len, &header_len))
    {
      self->info.ip_version = 6;
      return _parse_proxy_v1_tcp_header(self, proxy_line + header_len, proxy_line_len - header_len);
    }

  return FALSE;
}

static gboolean
_parse_proxy_v2_proxy_address(LogTransportHAProxy *self)
{
  gint address_family = (self->proto_v2.header->fam & 0xF0) >> 4;
  union proxy_addr *proxy_addr = self->proto_v2.addr;

  if (address_family == 1 && self->proto_v2.header_len >= sizeof(proxy_addr->ipv4_addr))
    {
      /* AF_INET */
      inet_ntop(AF_INET, (gchar *) &proxy_addr->ipv4_addr.src_addr, self->info.src_ip, sizeof(self->info.src_ip));
      inet_ntop(AF_INET, (gchar *) &proxy_addr->ipv4_addr.dst_addr, self->info.dst_ip, sizeof(self->info.dst_ip));
      self->info.src_port = ntohs(proxy_addr->ipv4_addr.src_port);
      self->info.dst_port = ntohs(proxy_addr->ipv4_addr.dst_port);
      self->info.ip_version = 4;
      self->proto_v2.addr_len = sizeof(proxy_addr->ipv4_addr);
      return TRUE;
    }
  else if (address_family == 2 && self->proto_v2.header_len >= sizeof(proxy_addr->ipv6_addr))
    {
      /* AF_INET6 */
      inet_ntop(AF_INET6, (gchar *) &proxy_addr->ipv6_addr.src_addr, self->info.src_ip, sizeof(self->info.src_ip));
      inet_ntop(AF_INET6, (gchar *) &proxy_addr->ipv6_addr.dst_addr, self->info.dst_ip, sizeof(self->info.dst_ip));
      self->info.src_port = ntohs(proxy_addr->ipv6_addr.src_port);
      self->info.dst_port = ntohs(proxy_addr->ipv6_addr.dst_port);
      self->info.ip_version = 6;
      self->proto_v2.addr_len = sizeof(proxy_addr->ipv6_addr);
      return TRUE;
    }
  else if (address_family == 0)
    {
      /* AF_UNSPEC */
      self->info.unknown = TRUE;
      self->proto_v2.addr_len = sizeof(proxy_addr->unspec_addr);
      return TRUE;
    }

  msg_error("PROXYv2 header does not have enough bytes to represent endpoint addresses or unknown address_family",
            evt_tag_int("proxy_header_len", self->proto_v2.header_len),
            evt_tag_int("address_family", address_family));
  return FALSE;
}

#define PTRDIFF(a, b) (ptrdiff_t) (((gchar *) (a)) - ((gchar *) (b)))
#define PP_TF_SKIP 0x01
#define PP_TF_TEXT 0x02

struct pp_type_desc
{
  const gchar *desc;
  guint flags;
} pp_type_desc[] =
{
  [PP2_TYPE_ALPN] = { .desc = "ALPN", .flags = PP_TF_TEXT },
  [PP2_TYPE_AUTHORITY] = { .desc = "Authority", .flags = PP_TF_TEXT },
  [PP2_TYPE_CRC32C] = { .desc = "CRC32", .flags = PP_TF_SKIP },
  [PP2_TYPE_NOOP] = { .desc = "NOOP", .flags = PP_TF_SKIP },
  [PP2_TYPE_UNIQUE_ID] = { .desc = "UniqueID", .flags = PP_TF_TEXT },
  [PP2_TYPE_SSL] = { .desc = "SSL" },
  [PP2_SUBTYPE_SSL_VERSION] = { 0 },
  [PP2_SUBTYPE_SSL_CN] = { 0 },
  [PP2_SUBTYPE_SSL_CIPHER] = { 0 },
  [PP2_SUBTYPE_SSL_SIG_ALG] = { 0 },
  [PP2_SUBTYPE_SSL_KEY_ALG] = { 0 },
  [PP2_TYPE_NETNS] = { .desc = "NetNS", .flags = PP_TF_TEXT },
};

static gboolean
_iter_v2_tlvs(struct proxy_tlv *tlvs, gsize tlvs_len)
{
  guint tlv_len = 0;
  guint tlv_pos = 0;
  for (struct proxy_tlv *tlv = tlvs;
       PTRDIFF(tlv, tlvs) < tlvs_len;
       tlv = (struct proxy_tlv *) (tlv->value + tlv_len))
    {
      tlv_pos = PTRDIFF(tlv, tlvs);
      tlv_len = (tlv->length_hi << 8) + tlv->length_lo;
      if (tlv_pos + sizeof(*tlv) + tlv_len > tlvs_len)
        {
          msg_error("PROXY header TLV points outside of the proxy header",
                    evt_tag_int("type", tlv->type),
                    evt_tag_int("pos", tlv_pos),
                    evt_tag_int("length", tlv_len),
                    evt_tag_int("tlvs_length", tlvs_len));
          return FALSE;
        }

      struct pp_type_desc *td = NULL;
      if (tlv->type < G_N_ELEMENTS(pp_type_desc))
        {
          td = &pp_type_desc[tlv->type];
          if (!td->desc)
            td = NULL;
        }
      else
        {
          td = NULL;
        }
      if (td)
        {
          if (td->flags & PP_TF_SKIP)
            continue;
          msg_debug("PROXY header TLV",
                    evt_tag_int("type", tlv->type),
                    evt_tag_str("type", td->desc),
                    (td->flags & PP_TF_TEXT)
                    ? evt_tag_mem("value", tlv->value, tlv_len)
                    : evt_tag_memdump("value", tlv->value, tlv_len));
        }
      else
        {
          msg_debug("PROXY header TLV",
                    evt_tag_int("type", tlv->type),
                    evt_tag_memdump("value", tlv->value, tlv_len));
        }
    }
  return TRUE;
}

static gboolean
_parse_proxy_v2_tlvs(LogTransportHAProxy *self)
{
  g_assert(self->proto_v2.header_len >= self->proto_v2.addr_len);
  self->proto_v2.tlvs_len = self->proto_v2.header_len - self->proto_v2.addr_len;
  self->proto_v2.tlvs = (struct proxy_tlv *) ((gchar *) self->proto_v2.addr + self->proto_v2.addr_len);

  msg_trace("PROXY header TLV block details",
            evt_tag_int("version", self->proxy_header_version),
            evt_tag_int("header_len", self->proto_v2.header_len),
            evt_tag_int("addr_len", self->proto_v2.addr_len),
            evt_tag_int("tlvs_len", self->proto_v2.tlvs_len));

  return _iter_v2_tlvs(self->proto_v2.tlvs, self->proto_v2.tlvs_len);
}

static gboolean
_parse_proxy_v2_header(LogTransportHAProxy *self)
{
  self->proto_v2.header = (struct proxy_hdr_v2 *) self->proxy_header_buff;
  self->proto_v2.header_len = ntohs(self->proto_v2.header->len);

  struct proxy_hdr_v2 *proxy_hdr = self->proto_v2.header;

  /* is this proxy v2 */
  if ((proxy_hdr->ver_cmd & 0xF0) != 0x20)
    return FALSE;

  self->proto_v2.addr = (union proxy_addr *)(proxy_hdr + 1);
  if ((proxy_hdr->ver_cmd & 0xF) == 0)
    {
      /* LOCAL connection */
      self->info.unknown = TRUE;

    }
  else if ((proxy_hdr->ver_cmd & 0xF) == 1)
    {
      /* PROXY connection */
    }
  else
    return FALSE;

  if (!_parse_proxy_v2_proxy_address(self))
    return FALSE;
  if (!_parse_proxy_v2_tlvs(self))
    return FALSE;
  return TRUE;
}

gboolean
_parse_proxy_header(LogTransportHAProxy *self)
{
  if (self->proxy_header_version == 1)
    return _parse_proxy_v1_header(self);
  else if (self->proxy_header_version == 2)
    return _parse_proxy_v2_header(self);
  else
    g_assert_not_reached();
}

static Status
_fetch_chunk(LogTransportHAProxy *self, gsize upto_bytes)
{
  g_assert(upto_bytes <= sizeof(self->proxy_header_buff));
  if (self->proxy_header_buff_len < upto_bytes)
    {
      gssize rc = log_transport_adapter_read_method(&self->super.super,
                                                    &(self->proxy_header_buff[self->proxy_header_buff_len]),
                                                    upto_bytes - self->proxy_header_buff_len, NULL);
      if (rc < 0)
        {
          if (errno == EAGAIN)
            return STATUS_AGAIN;
          else
            {
              msg_error("I/O error occurred while reading proxy header",
                        evt_tag_error(EVT_TAG_OSERROR));
              return STATUS_ERROR;
            }
        }
      /* EOF without data */
      if (rc == 0)
        {
          return STATUS_EOF;
        }

      self->proxy_header_buff_len += rc;
    }
  if (self->proxy_header_buff_len == upto_bytes)
    return STATUS_SUCCESS;
  return STATUS_AGAIN;
}

static Status
_fetch_packet(LogTransportHAProxy *self)
{
  gssize rc = log_transport_adapter_read_method(&self->super.super,
                                                self->proxy_header_buff, sizeof(self->proxy_header_buff),
                                                NULL);
  if (rc < 0)
    {
      if (errno == EAGAIN)
        return STATUS_AGAIN;
      else
        {
          msg_error("I/O error occurred while reading proxy header",
                    evt_tag_error(EVT_TAG_OSERROR));
          return STATUS_ERROR;
        }
    }

  self->proxy_header_buff_len = rc;
  return STATUS_SUCCESS;
}

static inline Status
_fetch_until_newline(LogTransportHAProxy *self)
{
  /* leave 1 character for terminating zero.  We should have plenty of space
   * in our buffer, as the longest line we need to fetch is 107 bytes
   * including the line terminator characters. */

  while (self->proxy_header_buff_len < sizeof(self->proxy_header_buff) - 1)
    {
      Status status = _fetch_chunk(self, self->proxy_header_buff_len + 1);

      if (status != STATUS_SUCCESS)
        return status;

      if (self->proxy_header_buff[self->proxy_header_buff_len - 1] == '\n')
        {
          return STATUS_SUCCESS;
        }
    }

  msg_error("PROXY proto header with invalid header length",
            evt_tag_int("max_parsable_length", sizeof(self->proxy_header_buff)-1),
            evt_tag_int("max_length_by_spec", PROXY_PROTO_HDR_MAX_LEN_RFC),
            evt_tag_long("length", self->proxy_header_buff_len),
            evt_tag_str("header", (const gchar *)self->proxy_header_buff));
  return STATUS_ERROR;
}

static Status
_fetch_proxy_v2_payload(LogTransportHAProxy *self)
{
  struct proxy_hdr_v2 *hdr = (struct proxy_hdr_v2 *) self->proxy_header_buff;
  gsize proxy_header_len = sizeof(*hdr) + ntohs(hdr->len);

  if (proxy_header_len > sizeof(self->proxy_header_buff))
    {
      msg_error("PROXYv2 proto header with invalid header length",
                evt_tag_int("max_parsable_length", sizeof(self->proxy_header_buff)),
                evt_tag_long("length", proxy_header_len));
      return STATUS_ERROR;
    }

  return _fetch_chunk(self, proxy_header_len);
}

static gboolean
_is_proxy_version_v1(LogTransportHAProxy *self)
{
  if (self->proxy_header_buff_len < PROXY_PROTO_HDR_MAGIC_LEN)
    return FALSE;

  return memcmp(self->proxy_header_buff, "PROXY", PROXY_PROTO_HDR_MAGIC_LEN) == 0;
}

static gboolean
_is_proxy_version_v2(LogTransportHAProxy *self)
{
  if (self->proxy_header_buff_len < PROXY_PROTO_HDR_MAGIC_LEN)
    return FALSE;

  return memcmp(self->proxy_header_buff, "\x0D\x0A\x0D\x0A\x00", PROXY_PROTO_HDR_MAGIC_LEN) == 0;
}

static inline Status
_fetch_into_proxy_buffer(LogTransportHAProxy *self)
{
  Status status;

  switch (self->header_fetch_state)
    {
    case LPPTS_INITIAL:
      self->header_fetch_state = LPPTS_DETERMINE_VERSION;
    /* fallthrough */
    case LPPTS_DETERMINE_VERSION:
      status = _fetch_chunk(self, PROXY_PROTO_HDR_MAGIC_LEN);

      if (status != STATUS_SUCCESS)
        return status;

      if (_is_proxy_version_v1(self))
        {
          self->header_fetch_state = LPPTS_PROXY_V1_READ_LINE;
          self->proxy_header_version = 1;
          goto process_proxy_v1;
        }
      else if (_is_proxy_version_v2(self))
        {
          self->header_fetch_state = LPPTS_PROXY_V2_READ_HEADER;
          self->proxy_header_version = 2;
          goto process_proxy_v2;
        }
      else
        {
          msg_error("Unable to determine PROXY protocol version",
                    evt_tag_mem("proxy_header", self->proxy_header_buff, PROXY_PROTO_HDR_MAGIC_LEN));
          return STATUS_ERROR;
        }
      g_assert_not_reached();

    case LPPTS_PROXY_V1_READ_LINE:

process_proxy_v1:
      return _fetch_until_newline(self);
    case LPPTS_PROXY_V2_READ_HEADER:
process_proxy_v2:
      status = _fetch_chunk(self, sizeof(struct proxy_hdr_v2));
      if (status != STATUS_SUCCESS)
        return status;

      self->header_fetch_state = LPPTS_PROXY_V2_READ_PAYLOAD;
    /* fallthrough */
    case LPPTS_PROXY_V2_READ_PAYLOAD:
      return _fetch_proxy_v2_payload(self);
    case LPPTS_PROXY_V2_READ_PACKET:
      self->proxy_header_version = 2;
      return _fetch_packet(self);
    default:
      g_assert_not_reached();
    }
}

static void
_save_addresses(LogTransportHAProxy *self, LogTransportAuxData *aux)
{
  if (self->info.unknown)
    return;

  if (self->info.ip_version == 4)
    {
      log_transport_aux_data_set_peer_addr_ref(aux,
                                               g_sockaddr_inet_new(self->info.src_ip, self->info.src_port));
      log_transport_aux_data_set_local_addr_ref(aux,
                                                g_sockaddr_inet_new(self->info.dst_ip, self->info.dst_port));
    }
#if SYSLOG_NG_ENABLE_IPV6
  else if (self->info.ip_version == 6)
    {
      log_transport_aux_data_set_peer_addr_ref(aux,
                                               g_sockaddr_inet6_new(self->info.src_ip, self->info.src_port));
      log_transport_aux_data_set_local_addr_ref(aux,
                                                g_sockaddr_inet6_new(self->info.dst_ip, self->info.dst_port));
    }
#endif
}

static Status
_fetch_and_process_proxy_header(LogTransportHAProxy *self, LogTransportAuxData *aux)
{
  Status status = _fetch_into_proxy_buffer(self);

  if (status == STATUS_EOF)
    {
      /* truncated header */
      msg_error("Truncated PROXY protocol header",
                evt_tag_mem("header", self->proxy_header_buff, self->proxy_header_buff_len));
      return STATUS_ERROR;
    }
  else if (status != STATUS_SUCCESS)
    return status;

  gboolean parsable = _parse_proxy_header(self);

  msg_debug("PROXY protocol header received",
            evt_tag_int("version", self->proxy_header_version),
            self->proxy_header_version == 1
            ? evt_tag_mem("header", self->proxy_header_buff, self->proxy_header_buff_len)
            : evt_tag_str("header", "<binary_data>"),
            evt_tag_int("health-check", self->info.unknown));

  if (parsable)
    {
      msg_trace("PROXY protocol header parsed successfully");

      if (self->info.unknown)
        return STATUS_EOF;

      _save_addresses(self, aux);

      return STATUS_SUCCESS;
    }
  else
    {
      msg_error("Error parsing PROXY protocol header");
      return STATUS_ERROR;
    }
}

static gssize
_haproxy_stream_read(LogTransport *s, gpointer buf, gsize buflen, LogTransportAuxData *aux)
{
  LogTransportHAProxy *self = (LogTransportHAProxy *)s;

  if (!self->proxy_header_processed)
    {
      LogTransportStack *stack = self->super.super.stack;
      aux = &stack->aux_data;

      Status status = _fetch_and_process_proxy_header(self, aux);
      if (status == STATUS_EOF)
        return 0;
      else if (status != STATUS_SUCCESS)
        {
          if (status == STATUS_ERROR)
            errno = EINVAL;
          else if (status == STATUS_AGAIN)
            errno = EAGAIN;
          return -1;
        }

      self->proxy_header_processed = TRUE;
      if (!log_transport_stack_switch(self->super.super.stack, self->switch_to))
        g_assert_not_reached();

      errno = EAGAIN;
      return -1;
    }
  g_assert_not_reached();
}

static gssize
_haproxy_dgram_read(LogTransport *s, gpointer buf, gsize buflen, LogTransportAuxData *aux)
{
  LogTransportHAProxy *self = (LogTransportHAProxy *)s;

  /* NOTE: in this case every packet contains both the PROXY header and the
   * payload */

  Status status = _fetch_and_process_proxy_header(self, aux);
  if (status == STATUS_EOF)
    return 0;
  else if (status != STATUS_SUCCESS)
    {
      if (status == STATUS_ERROR)
        errno = EINVAL;
      else if (status == STATUS_AGAIN)
        errno = EAGAIN;
      return -1;
    }

  gsize total_proxy_v2_size = sizeof(*self->proto_v2.header) + self->proto_v2.header_len;
  gsize payload_to_copy = MIN(buflen, self->proxy_header_buff_len - total_proxy_v2_size);
  if (payload_to_copy > 0)
    {
      memcpy(buf, &self->proxy_header_buff[total_proxy_v2_size], payload_to_copy);
      return payload_to_copy;
    }
  else
    {
      msg_debug("PROXY protocol is not followed by a payload in DGRAM mode",
                evt_tag_int("version", self->proxy_header_version),
                evt_tag_int("packet_size", self->proxy_header_buff_len),
                evt_tag_int("proxy_header_size", total_proxy_v2_size),
                evt_tag_int("payload_to_copy", payload_to_copy));
      errno = EAGAIN;
      return -1;
    }
  g_assert_not_reached();
}

LogTransport *
log_transport_haproxy_new(LogTransportIndex base, LogTransportIndex switch_to, gint sock_type)
{
  LogTransportHAProxy *self = g_new0(LogTransportHAProxy, 1);

  log_transport_adapter_init_instance(&self->super, "haproxy", base);
  self->switch_to = switch_to;

  if (sock_type == SOCK_DGRAM)
    {
      /* for UDP, only v2 is supported */
      self->super.super.read = _haproxy_dgram_read;
      self->header_fetch_state = LPPTS_PROXY_V2_READ_PACKET;
    }
  else if (sock_type == SOCK_STREAM)
    {
      self->super.super.read = _haproxy_stream_read;
      self->header_fetch_state = LPPTS_INITIAL;
    }
  else
    {
      g_assert_not_reached();
    }

  return &self->super.super;
}
