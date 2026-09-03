/*
 * Copyright (c) 2026 Axoflow
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

#include "altp-proto-parser.h"
#include "altp-proto-grammar.h"

extern int altp_proto_debug;

int altp_proto_parse(CfgLexer *lexer, LogProtoServerFactory **instance, gpointer arg);

static CfgLexerKeyword altp_proto_keywords[] =
{
  { "altp",               KW_ALTP },
  { "ack_timeout",        KW_ACK_TIMEOUT },
  { "session_expiration", KW_SESSION_EXPIRATION },
  { "tls_policy",         KW_TLS_POLICY },
  { "required",           KW_REQUIRED },
  { "optional",           KW_OPTIONAL },
  { NULL }
};

CfgParser altp_proto_parser =
{
#if SYSLOG_NG_ENABLE_DEBUG
  .debug_flag = &altp_proto_debug,
#endif
  .name = "altp",
  .keywords = altp_proto_keywords,
  .parse = (gint (*)(CfgLexer *, gpointer *, gpointer)) altp_proto_parse,
  /* the factory belongs to the receiver context of the options */
  .cleanup = NULL,
};

CFG_PARSER_IMPLEMENT_LEXER_BINDING(altp_proto_, ALTP_PROTO_, LogProtoServerFactory **)
