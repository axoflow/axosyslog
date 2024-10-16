/*
 * Copyright (c) 2023 Axoflow
 * Copyright (c) 2024 shifter
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#ifndef FILTERX_FUNC_EVENT_PARSER_TEST_HELPERS
#define FILTERX_FUNC_EVENT_PARSER_TEST_HELPERS

#include "filterx/filterx-eval.h"
#include "filterx/expr-function.h"

typedef FilterXExpr *(*event_parser_constructor)(FilterXFunctionArgs *args, GError **error);

void set_constructor(event_parser_constructor c);

FilterXFunctionArg *_create_arg(const gchar *key, FilterXExpr *val);
FilterXFunctionArg *_create_msg_arg(const gchar *input);
FilterXFunctionArg *_create_pair_separator_arg(const gchar *pair_separator);
FilterXFunctionArg *_create_value_separator_arg(const gchar *value_separator);

FilterXFunctionArgs *_assert_create_args_inner(va_list vargs);
FilterXFunctionArgs *_assert_create_args(int count, ...);

FilterXExpr *_new_parser(FilterXFunctionArgs *args, GError **error, FilterXObject *fillable);
FilterXObject *_eval_input_inner(GError **error, va_list vargs);
FilterXObject *_eval_input(GError **error, ...);
void _assert_parser_result_inner(const gchar *expected_result, ...);
void _assert_parser_result(const gchar *input, const gchar *expected_result);

#endif
