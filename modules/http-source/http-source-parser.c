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

#include "driver.h"
#include "cfg-parser.h"
#include "http-source-grammar.h"

extern int http_source_debug;

int http_source_parse(CfgLexer *lexer, LogDriver **instance, gpointer arg);

static CfgLexerKeyword http_source_keywords[] =
{
  { "http",               KW_HTTP },
  { "port",               KW_PORT },
  { "ip",                 KW_LOCALIP },
  { "localip",            KW_LOCALIP },
  { "path",               KW_PATH },
  { "max_request_size",   KW_MAX_REQUEST_SIZE },
  { "timeout",            KW_TIMEOUT },
  { NULL }
};

CfgParser http_source_parser =
{
#if SYSLOG_NG_ENABLE_DEBUG
  .debug_flag = &http_source_debug,
#endif
  .name = "http",
  .keywords = http_source_keywords,
  .parse = (gint (*)(CfgLexer *, gpointer *, gpointer)) http_source_parse,
  .cleanup = (void (*)(gpointer)) log_pipe_unref,
};

CFG_PARSER_IMPLEMENT_LEXER_BINDING(http_source_, HTTPSOURCE_, LogDriver **)
