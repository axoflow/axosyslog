/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 Attila Szakacs <attila.szakacs@axoflow.com>
 * Copyright (c) 2023 László Várady
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
#include "bigquery-grammar.h"
#include "grpc-parser.h"

extern int bigquery_debug;

int bigquery_parse(CfgLexer *lexer, LogDriver **instance, gpointer arg);

static CfgLexerKeyword bigquery_keywords[] =
{
  GRPC_KEYWORDS,
  { "bigquery", KW_BIGQUERY },
  { "project", KW_PROJECT },
  { "dataset", KW_DATASET },
  { "table", KW_TABLE },
  { NULL }
};

CfgParser bigquery_parser =
{
#if SYSLOG_NG_ENABLE_DEBUG
  .debug_flag = &bigquery_debug,
#endif
  .name = "bigquery",
  .keywords = bigquery_keywords,
  .parse = (gint (*)(CfgLexer *, gpointer *, gpointer)) bigquery_parse,
  .cleanup = (void (*)(gpointer)) log_pipe_unref,
};

CFG_PARSER_IMPLEMENT_LEXER_BINDING(bigquery_, BIGQUERY_, LogDriver **)
