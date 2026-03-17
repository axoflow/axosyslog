/*
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

#include "driver.h"
#include "cfg-parser.h"
#include "http-adapters-grammar.h"

extern int http_adapters_debug;

int http_adapters_parse(CfgLexer *lexer, LogDriver **instance, gpointer arg);

static CfgLexerKeyword http_adapters_keywords[] =
{
  { "response_adapter", KW_RESPONSE_ADAPTER },
  { NULL }
};

CfgParser http_adapters_parser =
{
#if SYSLOG_NG_ENABLE_DEBUG
  .debug_flag = &http_adapters_debug,
#endif
  .name = "http-adapters",
  .keywords = http_adapters_keywords,
  .parse = (gint (*)(CfgLexer *, gpointer *, gpointer)) http_adapters_parse,
  .cleanup = (void (*)(gpointer)) log_pipe_unref,
};

CFG_PARSER_IMPLEMENT_LEXER_BINDING(http_adapters_, HTTP_ADAPTERS_, LogDriver **)
