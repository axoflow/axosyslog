/*
 * Copyright (c) 2023 Axoflow
 * Copyright (c) 2024 shifter
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

#ifndef FILTERX_EXPR_REGEXP_SUBST_H_INCLUDED
#define FILTERX_EXPR_REGEXP_SUBST_H_INCLUDED

#include "filterx/filterx-expr.h"
#include "filterx/expr-generator.h"
#include "filterx/expr-function.h"
#include "filterx/func-flags.h"

DEFINE_FUNC_FLAGS(FilterXRegexpSubstFlags,
                  FILTERX_FUNC_REGEXP_SUBST_FLAG_JIT,
                  FILTERX_FUNC_REGEXP_SUBST_FLAG_GLOBAL,
                  FILTERX_FUNC_REGEXP_SUBST_FLAG_UTF8,
                  FILTERX_FUNC_REGEXP_SUBST_FLAG_IGNORECASE,
                  FILTERX_FUNC_REGEXP_SUBST_FLAG_NEWLINE,
                  FILTERX_FUNC_REGEXP_SUBST_FLAG_GROUPS
                 );

#define FILTERX_FUNC_REGEXP_SUBST_FLAG_JIT_NAME "jit"
#define FILTERX_FUNC_REGEXP_SUBST_FLAG_GLOBAL_NAME "global"
#define FILTERX_FUNC_REGEXP_SUBST_FLAG_UTF8_NAME "utf8"
#define FILTERX_FUNC_REGEXP_SUBST_FLAG_IGNORECASE_NAME "ignorecase"
#define FILTERX_FUNC_REGEXP_SUBST_FLAG_NEWLINE_NAME "newline"
#define FILTERX_FUNC_REGEXP_SUBST_FLAG_GROUPS_NAME "groups"

extern const char *FilterXRegexpSubstFlags_NAMES[];

FilterXExpr *filterx_function_regexp_subst_new(FilterXFunctionArgs *args, GError **error);
gboolean filterx_regexp_subst_is_jit_enabled(FilterXExpr *s);

#endif
