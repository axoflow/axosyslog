/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2023-2024 Attila Szakacs <attila.szakacs@axoflow.com>
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
#include "otel-grammar.h"
#include "grpc-parser.h"

extern int otel_debug;

int otel_parse(CfgLexer *lexer, void **instance, gpointer arg);

static CfgLexerKeyword otel_keywords[] =
{
  GRPC_KEYWORDS,
  { "opentelemetry",             KW_OPENTELEMETRY },
  { "axosyslog_otlp",            KW_AXOSYSLOG_OTLP },
  { "syslog_ng_otlp",            KW_AXOSYSLOG_OTLP },
  { "set_hostname",              KW_SET_HOSTNAME },
  { "keep_alive", KW_KEEP_ALIVE },
  { NULL }
};

CfgParser otel_parser =
{
#if SYSLOG_NG_ENABLE_DEBUG
  .debug_flag = &otel_debug,
#endif
  .name = "opentelemetry",
  .keywords = otel_keywords,
  .parse = (gint (*)(CfgLexer *, gpointer *, gpointer)) otel_parse,
  .cleanup = (void (*)(gpointer)) log_pipe_unref,
};

CFG_PARSER_IMPLEMENT_LEXER_BINDING(otel_, otel_, void **)
