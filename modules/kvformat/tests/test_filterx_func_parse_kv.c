/*
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
 */

#include <criterion/criterion.h>
#include "libtest/msg_parse_lib.h"
#include "libtest/filterx-lib.h"

#include "kv-parser.h"
#include "apphook.h"
#include "scratch-buffers.h"

#include "filterx/object-string.h"
#include "filterx/object-null.h"
#include "filterx/expr-literal.h"
#include "filterx/object-dict.h"
#include "filterx/object-dict-interface.h"
#include "filterx-func-parse-kv.h"


FilterXExpr *
_kv_new(FilterXFunctionArgs *args, GError **error, FilterXObject *fillable)
{
  FilterXExpr *func = filterx_function_parse_kv_new(args, error);

  if (!func)
    return NULL;

  FilterXExpr *fillable_expr = filterx_non_literal_new(fillable);
  filterx_generator_set_fillable(func, fillable_expr);

  return func;
}

Test(filterx_func_parse_kv, test_empty_args_error)
{
  GError *err = NULL;
  GError *args_err = NULL;
  FilterXExpr *func = _kv_new(filterx_function_args_new(NULL, &args_err), &err, filterx_dict_new());

  cr_assert_null(func);
  cr_assert_null(args_err);
  cr_assert_not_null(err);
  cr_assert(strstr(err->message, FILTERX_FUNC_PARSE_KV_USAGE) != NULL);
  g_error_free(err);
}


Test(filterx_func_parse_kv, test_skipped_opts_causes_default_behaviour)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new("foo=bar, bar=baz",
                                                      -1))));

  GError *err = NULL;
  GError *args_err = NULL;
  FilterXExpr *func = _kv_new(filterx_function_args_new(args, &args_err), &err, filterx_dict_new());

  cr_assert_null(args_err);
  cr_assert_null(err);

  FilterXObject *obj = init_and_eval_expr(func);

  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(dict)));

  GString *repr = scratch_buffers_alloc();

  LogMessageValueType lmvt;
  gboolean ok = filterx_object_marshal(obj, repr, &lmvt);

  cr_assert(ok);

  cr_assert_str_eq(repr->str, "{\"foo\":\"bar\",\"bar\":\"baz\"}");
  filterx_expr_unref(func);
  filterx_object_unref(obj);
  g_error_free(err);
}

Test(filterx_func_parse_kv, test_optional_value_separator_option_first_character)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new("foo@bar, bar@baz",
                                                      -1))));
  args = g_list_append(args, filterx_function_arg_new("value_separator", filterx_literal_new(filterx_string_new("@#$",
                                                      -1))));

  GError *err = NULL;
  GError *args_err = NULL;
  FilterXExpr *func = _kv_new(filterx_function_args_new(args, &args_err), &err, filterx_dict_new());

  cr_assert_null(args_err);
  cr_assert_null(err);

  FilterXObject *obj = init_and_eval_expr(func);

  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(dict)));

  GString *repr = scratch_buffers_alloc();

  LogMessageValueType lmvt;
  gboolean ok = filterx_object_marshal(obj, repr, &lmvt);

  cr_assert(ok);

  cr_assert_str_eq(repr->str, "{\"foo\":\"bar\",\"bar\":\"baz\"}");
  filterx_expr_unref(func);
  filterx_object_unref(obj);
  g_error_free(err);
}

Test(filterx_func_parse_kv, test_optional_empty_value_separator_option)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new("foo=bar, bar=baz",
                                                      -1))));
  args = g_list_append(args, filterx_function_arg_new("value_separator", filterx_literal_new(filterx_string_new("",
                                                      -1))));

  GError *err = NULL;
  GError *args_err = NULL;
  FilterXExpr *func = _kv_new(filterx_function_args_new(args, &args_err), &err, filterx_dict_new());

  cr_assert_null(func);
  cr_assert_null(args_err);
  cr_assert_not_null(err);

  cr_assert(strstr(err->message, FILTERX_FUNC_PARSE_KV_USAGE) != NULL);

  g_error_free(err);
}

Test(filterx_func_parse_kv, test_optional_pair_separator_option)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new("foo=bar-=|=-bar=baz",
                                                      -1))));
  args = g_list_append(args, filterx_function_arg_new("pair_separator", filterx_literal_new(filterx_string_new("-=|=-",
                                                      -1))));

  GError *err = NULL;
  GError *args_err = NULL;
  FilterXExpr *func = _kv_new(filterx_function_args_new(args, &args_err), &err, filterx_dict_new());

  cr_assert_null(args_err);
  cr_assert_null(err);

  FilterXObject *obj = init_and_eval_expr(func);

  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(dict)));

  GString *repr = scratch_buffers_alloc();

  LogMessageValueType lmvt;
  gboolean ok = filterx_object_marshal(obj, repr, &lmvt);

  cr_assert(ok);

  cr_assert_str_eq(repr->str, "{\"foo\":\"bar\",\"bar\":\"baz\"}");
  filterx_expr_unref(func);
  filterx_object_unref(obj);
  g_error_free(err);
}

Test(filterx_func_parse_kv, test_optional_stray_words_key_option)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL,
                                                      filterx_literal_new(filterx_string_new("foo=bar, lookslikenonKV bar=baz", -1))));
  args = g_list_append(args, filterx_function_arg_new("stray_words_key",
                                                      filterx_literal_new(filterx_string_new("straywords",
                                                          -1))));

  GError *err = NULL;
  GError *args_err = NULL;
  FilterXExpr *func = _kv_new(filterx_function_args_new(args, &args_err), &err, filterx_dict_new());

  cr_assert_null(args_err);
  cr_assert_null(err);

  FilterXObject *obj = init_and_eval_expr(func);

  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(dict)));

  GString *repr = scratch_buffers_alloc();

  LogMessageValueType lmvt;
  gboolean ok = filterx_object_marshal(obj, repr, &lmvt);

  cr_assert(ok);

  cr_assert_str_eq(repr->str, "{\"foo\":\"bar\",\"bar\":\"baz\",\"straywords\":\"lookslikenonKV\"}");
  filterx_expr_unref(func);
  filterx_object_unref(obj);
  g_error_free(err);
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

TestSuite(filterx_func_parse_kv, .init = setup, .fini = teardown);
