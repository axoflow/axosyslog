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

#ifndef FILTERX_AST_H_INCLUDED
#define FILTERX_AST_H_INCLUDED

#include "filterx/filterx-expr.h"
#include "compat/json.h"

/* Format the abstract syntax tree of a filterx expression as JSON: each node
 * reports its type, its verbatim source text, its location and the list of its
 * children. */
struct json_object *filterx_expr_format_ast(FilterXExpr *expr);

/* The pretty printed form of filterx_expr_format_ast(), the returned GString is
 * owned by the caller. */
GString *filterx_expr_format_ast_string(FilterXExpr *expr);

#endif
