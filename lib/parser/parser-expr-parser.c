/*
 * Copyright (c) 2002-2012 Balabit
 * Copyright (c) 1998-2012 Balázs Scheidler
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
#include "parser/parser-expr-parser.h"
#include "parser/parser-expr.h"
#include "parser/parser-expr-grammar.h"

extern int parser_expr_debug;
int parser_expr_parse(CfgLexer *lexer, LogExprNode **node, gpointer arg);

static CfgLexerKeyword parser_expr_keywords[] =
{
  { NULL }
};

CfgParser parser_expr_parser =
{
#if SYSLOG_NG_ENABLE_DEBUG
  .debug_flag = &parser_expr_debug,
#endif
  .name = "parser expression",
  .context = LL_CONTEXT_PARSER,
  .keywords = parser_expr_keywords,
  .parse = (gint (*)(CfgLexer *, gpointer *, gpointer)) parser_expr_parse,
};

CFG_PARSER_IMPLEMENT_LEXER_BINDING(parser_expr_, PARSER_EXPR_, LogExprNode **)
