/*
 * Copyright (c) 2024 Attila Szakacs
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#ifndef FILTERX_EXPR_REGEXP_H_INCLUDED
#define FILTERX_EXPR_REGEXP_H_INCLUDED

#include "filterx/filterx-expr.h"
#include "filterx/expr-generator.h"
#include "filterx/expr-function.h"

#define FILTERX_FUNC_REGEXP_SUBST_FLAG_JIT_NAME "jit"
#define FILTERX_FUNC_REGEXP_SUBST_FLAG_GLOBAL_NAME "global"
#define FILTERX_FUNC_REGEXP_SUBST_FLAG_UTF8_NAME "utf8"
#define FILTERX_FUNC_REGEXP_SUBST_FLAG_IGNORECASE_NAME "ignorecase"
#define FILTERX_FUNC_REGEXP_SUBST_FLAG_NEWLINE_NAME "newline"

typedef struct FilterXFuncRegexpSubstOpts_
{
  gboolean global;
  gboolean jit;
  gboolean utf8;
  gboolean ignorecase;
  gboolean newline;
} FilterXFuncRegexpSubstOpts;

FilterXExpr *filterx_expr_regexp_match_new(FilterXExpr *lhs, const gchar *pattern);
FilterXExpr *filterx_expr_regexp_nomatch_new(FilterXExpr *lhs, const gchar *pattern);
FilterXExpr *filterx_expr_regexp_search_generator_new(FilterXExpr *lhs, const gchar *pattern);
FilterXExpr *filterx_function_regexp_subst_new(const gchar *function_name, FilterXFunctionArgs *args, GError **error);
gboolean filterx_regexp_subst_is_jit_enabled(FilterXExpr *s);

#endif
