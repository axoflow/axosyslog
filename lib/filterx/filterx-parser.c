/*
 * Copyright (c) 2023 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#include "filterx/filterx-parser.h"
#include "filterx/filterx-grammar.h"

extern int filterx_debug;
int filterx_parse(CfgLexer *lexer, FilterXExpr **expr, gpointer arg);

static CfgLexerKeyword filterx_keywords[] =
{
  { "or",                 KW_OR },
  { "and",                KW_AND },
  { "in",                 KW_IN },
  { "not",                KW_NOT },
  { "lt",                 KW_STR_LT },
  { "le",                 KW_STR_LE },
  { "eq",                 KW_STR_EQ },
  { "ne",                 KW_STR_NE },
  { "ge",                 KW_STR_GE },
  { "gt",                 KW_STR_GT },

  { "true",               KW_TRUE },
  { "false",              KW_FALSE },
  { "null",               KW_NULL },
  { "enum",               KW_ENUM },

  { "if",                 KW_IF },
  { "else",               KW_ELSE },
  { "elif",               KW_ELIF },
  { "switch",             KW_SWITCH },
  { "case",               KW_CASE },
  { "default",            KW_DEFAULT },

  { "isset",              KW_ISSET },
  { "declare",            KW_DECLARE },
  { "drop",               KW_DROP },
  { "done",               KW_DONE },
  { "break",              KW_BREAK },

  { CFG_KEYWORD_STOP },
};

CfgParser filterx_parser =
{
#if SYSLOG_NG_ENABLE_DEBUG
  .debug_flag = &filterx_debug,
#endif
  .name = "filterx expression",
  .context = LL_CONTEXT_FILTERX,
  .keywords = filterx_keywords,
  .parse = (gint (*)(CfgLexer *, gpointer *, gpointer)) filterx_parse,
};

CFG_PARSER_IMPLEMENT_LEXER_BINDING(filterx_, FILTERX_, FilterXExpr **)
