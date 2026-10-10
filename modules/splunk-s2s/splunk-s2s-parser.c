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

#include "splunk-s2s-parser.h"
#include "splunk-s2s-grammar.h"

extern int splunk_s2s_debug;

int splunk_s2s_parse(CfgLexer *lexer, LogProtoServerFactory **instance, gpointer arg);

/* multi-line-mode(), multi-line-prefix() and multi-line-garbage() are
 * keywords of the core grammar and need no entry here */
static CfgLexerKeyword splunk_s2s_keywords[] =
{
  { "splunk_s2s",         KW_SPLUNK_S2S },
  { "multi_line_timeout", KW_MULTI_LINE_TIMEOUT },
  { NULL }
};

CfgParser splunk_s2s_parser =
{
#if SYSLOG_NG_ENABLE_DEBUG
  .debug_flag = &splunk_s2s_debug,
#endif
  .name = "splunk-s2s",
  .keywords = splunk_s2s_keywords,
  .parse = (gint (*)(CfgLexer *, gpointer *, gpointer)) splunk_s2s_parse,
  /* the factory is static, there is nothing to clean up */
  .cleanup = NULL,
};

CFG_PARSER_IMPLEMENT_LEXER_BINDING(splunk_s2s_, SPLUNK_S2S_, LogProtoServerFactory **)
