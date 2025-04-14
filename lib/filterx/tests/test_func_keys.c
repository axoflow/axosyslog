/*
 * Copyright (c) 2024 Axoflow
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

#include <criterion/criterion.h>
#include "libtest/filterx-lib.h"

#include "filterx/func-flatten.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "filterx/json-repr.h"
#include "filterx/expr-literal.h"
#include "filterx/func-keys.h"
#include "filterx/filterx-eval.h"
#include "filterx/object-dict-interface.h"
#include "filterx/object-list-interface.h"

#include "apphook.h"
#include "scratch-buffers.h"

Test(filterx_func_keys, empty_args)
{
  GList *args = NULL;

  GError *error = NULL;
  FilterXExpr *fn = filterx_function_keys_new(filterx_function_args_new(args, NULL), &error);
  cr_assert_null(fn);
  cr_assert_not_null(error);

  cr_assert_str_eq(error->message, FILTERX_FUNC_KEYS_ERR_NUM_ARGS FILTERX_FUNC_KEYS_USAGE);
  g_clear_error(&error);
}

Test(filterx_func_keys, invalid_args_number)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_non_literal_new(filterx_string_new("not a dict",
                                                      -1))));
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_non_literal_new(filterx_string_new("more args",
                                                      -1))));

  GError *error = NULL;
  FilterXExpr *fn = filterx_function_keys_new(filterx_function_args_new(args, NULL), &error);
  cr_assert_null(fn);
  cr_assert_not_null(error);

  cr_assert_str_eq(error->message, FILTERX_FUNC_KEYS_ERR_NUM_ARGS FILTERX_FUNC_KEYS_USAGE);
  g_clear_error(&error);
}

Test(filterx_func_keys, invalid_arg_type)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_non_literal_new(filterx_string_new("not a dict",
                                                      -1))));

  GError *error = NULL;
  FilterXExpr *fn = filterx_function_keys_new(filterx_function_args_new(args, NULL), &error);
  cr_assert_not_null(fn);
  cr_assert_null(error);
  FilterXObject *res = filterx_expr_eval(fn);
  cr_assert_null(res);

  const gchar *last_error = filterx_eval_get_last_error();

  cr_assert_str_eq(last_error, FILTERX_FUNC_KEYS_ERR_NONDICT FILTERX_FUNC_KEYS_USAGE);

  filterx_expr_unref(fn);
  g_clear_error(&error);
}

Test(filterx_func_keys, valid_input)
{
  GList *args = NULL;
  FilterXObject *dict = filterx_object_from_json("{\"foo\":1,\"bar\":[3,2,1],\"baz\":{\"tik\":\"tak\"}}", -1, NULL);
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_non_literal_new(dict)));

  GError *error = NULL;
  FilterXExpr *fn = filterx_function_keys_new(filterx_function_args_new(args, NULL), &error);
  cr_assert_not_null(fn);
  cr_assert_null(error);
  FilterXObject *res = filterx_expr_eval(fn);
  cr_assert_not_null(res);
  cr_assert(filterx_object_is_type(res, &FILTERX_TYPE_NAME(list)));

  assert_object_repr_equals(res, "[\"foo\",\"bar\",\"baz\"]");

  filterx_object_unref(res);
  filterx_expr_unref(fn);
  g_clear_error(&error);
}

static void
setup(void)
{
  app_startup();
  init_libtest_filterx();
}

static void
teardown(void)
{
  scratch_buffers_explicit_gc();
  deinit_libtest_filterx();
  app_shutdown();
}

TestSuite(filterx_func_keys, .init = setup, .fini = teardown);
