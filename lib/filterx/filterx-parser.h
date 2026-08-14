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

#ifndef FILTERX_EXPR_PARSER_H_INCLUDED
#define FILTERX_EXPR_PARSER_H_INCLUDED

#include "cfg-parser.h"
#include "filterx/filterx-expr.h"

extern CfgParser filterx_parser;

CFG_PARSER_DECLARE_LEXER_BINDING(filterx_, FILTERX_, FilterXExpr **)

/* Compile an entire input as filterx code, e.g. as if it were the body of a
 * filterx {} block, and throw away the result: this only validates that the code
 * compiles.  @lexer is consumed.  If @ast is not NULL, the abstract syntax tree
 * of the compiled script is returned there as JSON (owned by the caller), it is
 * left intact unless the script compiles. */
gboolean filterx_compile_script(GlobalConfig *cfg, CfgLexer *lexer, GString **ast);
gboolean filterx_compile_script_file(GlobalConfig *cfg, const gchar *fname, GString **ast);

#endif
