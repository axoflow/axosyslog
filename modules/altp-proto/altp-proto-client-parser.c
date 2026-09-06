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

#include "altp-proto-client-parser.h"
#include "altp-proto-client-grammar.h"

extern int altp_proto_client_debug;

int altp_proto_client_parse(CfgLexer *lexer, LogProtoClientFactory **instance, gpointer arg);

static CfgLexerKeyword altp_proto_client_keywords[] =
{
  { "altp",               KW_ALTP },
  { "ack_timeout",        KW_ACK_TIMEOUT },
  { "max_frame_size",     KW_MAX_FRAME_SIZE },
  { "compression",        KW_COMPRESSION },
  { "compression_level",  KW_COMPRESSION_LEVEL },
  { "tls_policy",         KW_TLS_POLICY },
  { "required",           KW_REQUIRED },
  { "optional",           KW_OPTIONAL },
  { "none",               KW_NONE },
  { "batch_size",         KW_BATCH_SIZE },
  { "response_timeout",   KW_RESPONSE_TIMEOUT },

  /* The spellings of existing ALTP deployments.  A word listed here shadows the
   * same word of an enclosing context, which is how flush-lines() means the
   * Batch bound of this Sender inside transport(altp()).
   */
  { "tls_required",       KW_TLS_REQUIRED },
  { "allow_plain_compress", KW_COMPRESSION },
  { "allow_compress",     KW_COMPRESSION, KWS_OBSOLETE, "compression()" },
  { "compress_level",     KW_COMPRESSION_LEVEL },
  { "message_acknowledgement_timeout", KW_ACK_TIMEOUT },
  { "flush_lines",        KW_BATCH_SIZE, KWS_OBSOLETE, "batch-size()" },
  { "flush_timeout",      KW_FLUSH_TIMEOUT, KWS_OBSOLETE, "flush-timeout() has no effect inside transport(altp())" },
  { NULL }
};

CfgParser altp_proto_client_parser =
{
#if SYSLOG_NG_ENABLE_DEBUG
  .debug_flag = &altp_proto_client_debug,
#endif
  .name = "altp",
  .keywords = altp_proto_client_keywords,
  .parse = (gint (*)(CfgLexer *, gpointer *, gpointer)) altp_proto_client_parse,
  /* the LogProtoClientFactory we return is a static of the module */
  .cleanup = NULL,
};

CFG_PARSER_IMPLEMENT_LEXER_BINDING(altp_proto_client_, ALTP_PROTO_CLIENT_, LogProtoClientFactory **)
