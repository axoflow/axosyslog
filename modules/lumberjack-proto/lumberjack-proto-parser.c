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

#include "lumberjack-proto-parser.h"
#include "lumberjack-proto-grammar.h"

extern int lumberjack_proto_debug;

int lumberjack_proto_parse(CfgLexer *lexer, LogProtoServerFactory **instance, gpointer arg);

static CfgLexerKeyword lumberjack_proto_keywords[] =
{
  { "lumberjack",         KW_LUMBERJACK },
  { "max_window_size",    KW_MAX_WINDOW_SIZE },
  { "keepalive_interval", KW_KEEPALIVE_INTERVAL },
  { "window_timeout",     KW_WINDOW_TIMEOUT },
  { NULL }
};

CfgParser lumberjack_proto_parser =
{
#if SYSLOG_NG_ENABLE_DEBUG
  .debug_flag = &lumberjack_proto_debug,
#endif
  .name = "lumberjack",
  .keywords = lumberjack_proto_keywords,
  .parse = (gint (*)(CfgLexer *, gpointer *, gpointer)) lumberjack_proto_parse,
  /* the factory is static, nothing to clean up */
  .cleanup = NULL,
};

CFG_PARSER_IMPLEMENT_LEXER_BINDING(lumberjack_proto_, LUMBERJACK_PROTO_, LogProtoServerFactory **)
