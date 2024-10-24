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

#include <criterion/criterion.h>
#include "libtest/filterx-lib.h"

#include "apphook.h"
#include "scratch-buffers.h"
#include "test_helpers.h"
#include "event-format-parser.h"

#include "filterx/object-string.h"
#include "filterx/object-null.h"
#include "filterx/expr-literal.h"
#include "filterx/object-json.h"
#include "filterx/object-list-interface.h"
#include "filterx/object-dict-interface.h"
#include "filterx/object-primitive.h"

static event_parser_constructor constructor = NULL;

void
set_constructor(event_parser_constructor c)
{
  constructor = c;
}

FilterXFunctionArg *
_create_arg(const gchar *key, FilterXExpr *val)
{
  return filterx_function_arg_new(key, val);
}

FilterXFunctionArg *
_create_msg_arg(const gchar *input)
{
  return _create_arg(NULL, filterx_literal_new(filterx_string_new(input, -1)));
}

FilterXFunctionArg *
_create_pair_separator_arg(const gchar *pair_separator)
{
  return _create_arg(EVENT_FORMAT_PARSER_ARG_NAME_PAIR_SEPARATOR, filterx_literal_new(filterx_string_new(pair_separator,
                     -1)));
}

FilterXFunctionArg *
_create_value_separator_arg(const gchar *value_separator)
{
  return _create_arg(EVENT_FORMAT_PARSER_ARG_NAME_VALUE_SEPARATOR, filterx_literal_new(filterx_string_new(value_separator,
                     -1)));
}

FilterXFunctionArgs *
_assert_create_args_inner(va_list vargs)
{
  GList *args = NULL;

  while (1)
    {
      FilterXFunctionArg *varg = va_arg(vargs, FilterXFunctionArg *);
      if (varg == NULL)
        break;
      args = g_list_append(args, varg);
    };

  GError *args_err = NULL;
  FilterXFunctionArgs *result = filterx_function_args_new(args, &args_err);
  cr_assert_null(args_err);
  g_error_free(args_err);
  return result;
}

FilterXFunctionArgs *
_assert_create_args(int count, ...)
{
  va_list vargs;
  va_start(vargs, count);
  FilterXFunctionArgs *result = _assert_create_args_inner(vargs);
  va_end(vargs);
  return result;
}

FilterXExpr *
_new_parser(FilterXFunctionArgs *args, GError **error, FilterXObject *fillable)
{
  if (constructor == NULL)
    goto error;
  FilterXExpr *func = constructor(args, error);

  if (!func)
    goto error;

  FilterXExpr *fillable_expr = filterx_non_literal_new(fillable);
  filterx_generator_set_fillable(func, fillable_expr);

  return func;
error:
  filterx_object_unref(fillable);
  return NULL;
}

FilterXObject *
_eval_input_inner(GError **error, va_list vargs)
{
  FilterXExpr *func = _new_parser(_assert_create_args_inner(vargs), error, filterx_json_object_new_empty());
  cr_assert_not_null(func);
  FilterXObject *obj = filterx_expr_eval(func);
  filterx_expr_unref(func);
  return obj;
}

FilterXObject *
_eval_input(GError **error, ...)
{
  va_list vargs;
  va_start(vargs, error);
  FilterXObject *obj = _eval_input_inner(error, vargs);
  va_end(vargs);
  return obj;
}

void
_assert_parser_result_inner(const gchar *expected_result, ...)
{
  va_list vargs;
  va_start(vargs, expected_result);

  GError *err = NULL;
  FilterXObject *obj = _eval_input_inner(&err, vargs);
  cr_assert_null(err);
  cr_assert_not_null(obj);

  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(dict)));

  GString *repr = scratch_buffers_alloc();

  LogMessageValueType lmvt;
  cr_assert(filterx_object_marshal(obj, repr, &lmvt));

  cr_assert_str_eq(repr->str, expected_result);

  filterx_object_unref(obj);
  g_error_free(err);
  va_end(vargs);
}

void
_assert_parser_result(const gchar *input, const gchar *expected_result)
{
  _assert_parser_result_inner(expected_result, _create_msg_arg(input), NULL);
}
