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

#ifndef HTTP_SOURCE_PARSER_H_INCLUDED
#define HTTP_SOURCE_PARSER_H_INCLUDED

#define HTTP_SOURCE_KEYWORDS \
  { "tls",                KW_TLS }, \
  { "peer_verify",        KW_PEER_VERIFY }, \
  { "key_file",           KW_KEY_FILE }, \
  { "cert_file",          KW_CERT_FILE }, \
  { "dhparam_file",       KW_DHPARAM_FILE }, \
  { "pkcs12_file",        KW_PKCS12_FILE }, \
  { "ca_dir",             KW_CA_DIR }, \
  { "crl_dir",            KW_CRL_DIR }, \
  { "trusted_keys",       KW_TRUSTED_KEYS }, \
  { "trusted_dn",         KW_TRUSTED_DN }, \
  { "cipher_suite",       KW_CIPHER_SUITE }, \
  { "ecdh_curve_list",    KW_ECDH_CURVE_LIST }, \
  { "curve_list",         KW_ECDH_CURVE_LIST, KWS_OBSOLETE, "ecdh_curve_list"}, \
  { "ssl_options",        KW_SSL_OPTIONS }, \
  { "localip",            KW_LOCALIP }, \
  { "ip",                 KW_IP }, \
  { "localport",          KW_LOCALPORT }, \
  { "port",               KW_PORT }, \
  { "ip_ttl",             KW_IP_TTL }, \
  { "ip_tos",             KW_IP_TOS }, \
  { "ip_freebind",        KW_IP_FREEBIND }, \
  { "so_broadcast",       KW_SO_BROADCAST }, \
  { "so_rcvbuf",          KW_SO_RCVBUF }, \
  { "so_sndbuf",          KW_SO_SNDBUF }, \
  { "so_keepalive",       KW_SO_KEEPALIVE }, \
  { "tcp_keep_alive",     KW_SO_KEEPALIVE }, \
  { "tcp_keepalive",      KW_SO_KEEPALIVE }, \
  { "tcp_keepalive_time", KW_TCP_KEEPALIVE_TIME }, \
  { "tcp_keepalive_probes", KW_TCP_KEEPALIVE_PROBES }, \
  { "tcp_keepalive_intvl", KW_TCP_KEEPALIVE_INTVL }, \
  { "transport",          KW_TRANSPORT }, \
  { "ip_protocol",        KW_IP_PROTOCOL }, \
  { "max_connections",    KW_MAX_CONNECTIONS }, \
  { "max_request_size",   KW_MAX_REQUEST_SIZE }, \
  { "listen_backlog",     KW_LISTEN_BACKLOG }, \
  { "keep_alive",         KW_KEEP_ALIVE }

#endif
