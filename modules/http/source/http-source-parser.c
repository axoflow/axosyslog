/*
 * Copyright (c) 2018 Balabit
 * Copyright (c) 2018 László Várady <laszlo.varady@balabit.com>
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
#include "http/source/http-source-parser.h"

extern int http_source_debug;

int http_source_parse(CfgLexer *lexer, LogDriver **instance, gpointer arg);

static CfgLexerKeyword http_source_keywords[] =
{
  { "ehttp", KW_EHTTP },
  { "mode", KW_MODE },
  HTTP_SOURCE_KEYWORDS,
  { NULL }
};

CfgParser http_source_parser =
{
#if SYSLOG_NG_ENABLE_DEBUG
  .debug_flag = &http_source_debug,
#endif
  .name = "http_source",
  .keywords = http_source_keywords,
  .parse = (gint (*)(CfgLexer *, gpointer *, gpointer)) http_source_parse,
  .cleanup = (void (*)(gpointer)) log_pipe_unref,
};

CFG_PARSER_IMPLEMENT_LEXER_BINDING(http_source_, HTTP_SOURCE_, LogDriver **)
