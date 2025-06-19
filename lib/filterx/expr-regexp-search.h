/*
 * Copyright (c) 2023 Axoflow
 * Copyright (c) 2024 shifter
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

#ifndef FILTERX_EXPR_REGEXP_SEARCH_H_INCLUDED
#define FILTERX_EXPR_REGEXP_SEARCH_H_INCLUDED

#include "filterx/filterx-expr.h"
#include "filterx/expr-generator.h"
#include "filterx/expr-function.h"
#include "filterx/func-flags.h"

DEFINE_FUNC_FLAGS(FilterXRegexpSearchFlags,
                  FILTERX_REGEXP_SEARCH_KEEP_GRP_ZERO,
                  FILTERX_REGEXP_SEARCH_LIST_MODE
                 );

#define FILTERX_REGEXP_SEARCH_KEEP_GRP_ZERO_NAME "keep_zero"
#define FILTERX_REGEXP_SEARCH_LIST_MODE_NAME "list_mode"

extern const char *FilterXRegexpSearchFlags_NAMES[];

FilterXExpr *filterx_function_regexp_search_new(FilterXFunctionArgs *args, GError **error);

#endif
