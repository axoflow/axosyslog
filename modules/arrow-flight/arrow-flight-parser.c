/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Attila Szakacs <attila.szakacs@axoflow.com>
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
#include "arrow-flight-grammar.h"

extern int arrow_flight_debug;

int arrow_flight_parse(CfgLexer *lexer, LogDriver **instance, gpointer arg);

static CfgLexerKeyword arrow_flight_keywords[] =
{
  { "arrow_flight",  KW_ARROW_FLIGHT },
  { "url",           KW_URL },
  { "path",          KW_PATH },
  { NULL }
};

CfgParser arrow_flight_parser =
{
#if SYSLOG_NG_ENABLE_DEBUG
  .debug_flag = &arrow_flight_debug,
#endif
  .name = "arrow-flight",
  .keywords = arrow_flight_keywords,
  .parse = (gint (*)(CfgLexer *, gpointer *, gpointer)) arrow_flight_parse,
  .cleanup = (void (*)(gpointer)) log_pipe_unref,
};

CFG_PARSER_IMPLEMENT_LEXER_BINDING(arrow_flight_, ARROW_FLIGHT_, LogDriver **)
