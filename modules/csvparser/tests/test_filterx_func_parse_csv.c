/*
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
 */

#include <criterion/criterion.h>
#include "libtest/filterx-lib.h"

#include "apphook.h"
#include "scratch-buffers.h"

#include "filterx/object-string.h"
#include "filterx/object-null.h"
#include "filterx/expr-literal.h"
#include "filterx/object-json.h"
#include "filterx-func-parse-csv.h"
#include "filterx/object-list-interface.h"
#include "scanner/csv-scanner/csv-scanner.h"
#include "filterx/object-primitive.h"
#include "filterx/filterx-object-istype.h"

static FilterXObject *
_generate_string_list(const gchar *elts, ...)
{
  FilterXObject *result = filterx_json_array_new_empty();

  va_list args;
  va_start(args, elts);

  const gchar *elt = elts;
  while (elt != NULL)
    {
      FilterXObject *str = filterx_string_new(elt, -1);
      cr_assert(filterx_list_append(result, &str));
      filterx_object_unref(str);
      elt = va_arg(args, const gchar *);
    }

  va_end(args);
  va_start(args, elts);

  return result;
}

FilterXExpr *
_csv_new(FilterXFunctionArgs *args, GError **error, FilterXObject *fillable)
{
  FilterXExpr *func = filterx_function_parse_csv_new(args, error);

  if (!func)
    return NULL;

  FilterXExpr *fillable_expr = filterx_non_literal_new(fillable);
  filterx_generator_set_fillable(func, fillable_expr);

  return func;
}

Test(filterx_func_parse_csv, test_helper_generate_string_list_empty)
{
  FilterXObject *col_names = _generate_string_list(NULL);
  cr_assert_not_null(col_names);

  GString *repr = scratch_buffers_alloc();
  LogMessageValueType lmvt;

  cr_assert(filterx_object_marshal(col_names, repr, &lmvt));
  cr_assert_str_eq(repr->str, "");
  filterx_object_unref(col_names);
}

Test(filterx_func_parse_csv, test_helper_generate_string_list)
{
  FilterXObject *col_names = _generate_string_list("1st", NULL);
  cr_assert_not_null(col_names);

  GString *repr = scratch_buffers_alloc();
  LogMessageValueType lmvt;

  cr_assert(filterx_object_marshal(col_names, repr, &lmvt));
  cr_assert_str_eq(repr->str, "1st");
  filterx_object_unref(col_names);
}

Test(filterx_func_parse_csv, test_helper_generate_string_list_multiple_elts)
{
  FilterXObject *col_names = _generate_string_list("1st", "2nd", "3rd", NULL);
  cr_assert_not_null(col_names);

  GString *repr = scratch_buffers_alloc();
  LogMessageValueType lmvt;

  cr_assert(filterx_object_marshal(col_names, repr, &lmvt));
  cr_assert_str_eq(repr->str, "1st,2nd,3rd");
  filterx_object_unref(col_names);
}

Test(filterx_func_parse_csv, test_empty_args_error)
{
  GError *err = NULL;
  GError *args_err = NULL;
  FilterXExpr *func = _csv_new(filterx_function_args_new(NULL, &args_err), &err, filterx_json_array_new_empty());
  cr_assert_null(args_err);
  cr_assert_null(func);
  cr_assert_not_null(err);
  cr_assert(strstr(err->message, FILTERX_FUNC_PARSE_CSV_USAGE) != NULL);
  g_error_free(err);
}


Test(filterx_func_parse_csv, test_skipped_opts_causes_default_behaviour)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL,
                                                      filterx_literal_new(filterx_string_new("foo,bar,baz,tik,tak,toe", -1))));

  GError *err = NULL;
  GError *args_err = NULL;
  FilterXExpr *func = _csv_new(filterx_function_args_new(args, &args_err), &err, filterx_json_array_new_empty());
  cr_assert_null(args_err);
  cr_assert_null(err);

  FilterXObject *obj = filterx_expr_eval(func);

  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(json_array)));

  GString *repr = scratch_buffers_alloc();

  LogMessageValueType lmvt;
  cr_assert(filterx_object_marshal(obj, repr, &lmvt));

  cr_assert_str_eq(repr->str, "foo,bar,baz,tik,tak,toe");
  filterx_expr_unref(func);
  filterx_object_unref(obj);
  g_error_free(err);
}

Test(filterx_func_parse_csv, test_set_optional_first_argument_column_names)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new("foo,bar,baz", -1))));
  FilterXObject *col_names = _generate_string_list("1st", "2nd", "3rd", NULL);
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_PARSE_CSV_ARG_NAME_COLUMNS,
                                                      filterx_literal_new(col_names)));

  GError *err = NULL;
  GError *args_err = NULL;
  FilterXExpr *func = _csv_new(filterx_function_args_new(args, &args_err), &err, filterx_json_object_new_empty());
  cr_assert_null(args_err);
  cr_assert_null(err);

  FilterXObject *obj = filterx_expr_eval(func);

  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(json_object)));

  GString *repr = scratch_buffers_alloc();

  LogMessageValueType lmvt;
  cr_assert(filterx_object_marshal(obj, repr, &lmvt));

  cr_assert_str_eq(repr->str, "{\"1st\":\"foo\",\"2nd\":\"bar\",\"3rd\":\"baz\"}");
  filterx_expr_unref(func);
  filterx_object_unref(obj);
  g_error_free(err);
}

Test(filterx_func_parse_csv, test_column_names_sets_expected_column_size_additional_columns_dropped)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL,
                                                      filterx_literal_new(filterx_string_new("foo,bar,baz,more,columns,we,did,not,expect", -1))));
  FilterXObject *col_names = _generate_string_list("1st", "2nd", "3rd", NULL); // sets expected column size 3
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_PARSE_CSV_ARG_NAME_COLUMNS,
                                                      filterx_literal_new(col_names)));

  GError *err = NULL;
  GError *args_err = NULL;
  FilterXExpr *func = _csv_new(filterx_function_args_new(args, &args_err), &err, filterx_json_object_new_empty());
  cr_assert_null(args_err);
  cr_assert_null(err);

  FilterXObject *obj = filterx_expr_eval(func);

  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(json_object)));

  GString *repr = scratch_buffers_alloc();

  LogMessageValueType lmvt;
  cr_assert(filterx_object_marshal(obj, repr, &lmvt));

  cr_assert_str_eq(repr->str, "{\"1st\":\"foo\",\"2nd\":\"bar\",\"3rd\":\"baz\"}");
  filterx_expr_unref(func);
  filterx_object_unref(obj);
  g_error_free(err);
}

Test(filterx_func_parse_csv, test_optional_argument_delimiters)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL,
                                                      filterx_literal_new(filterx_string_new("foo bar+baz;tik|tak:toe", -1))));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_PARSE_CSV_ARG_NAME_DELIMITER,
                                                      filterx_literal_new(filterx_string_new(" +;", -1))));

  GError *err = NULL;
  GError *args_err = NULL;
  FilterXExpr *func = _csv_new(filterx_function_args_new(args, &args_err), &err, filterx_json_array_new_empty());
  cr_assert_null(args_err);
  cr_assert_null(err);

  FilterXObject *obj = filterx_expr_eval(func);

  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(json_array)));

  GString *repr = scratch_buffers_alloc();

  LogMessageValueType lmvt;
  cr_assert(filterx_object_marshal(obj, repr, &lmvt));

  cr_assert_str_eq(repr->str, "foo,bar,baz,tik|tak:toe");
  filterx_expr_unref(func);
  filterx_object_unref(obj);
  g_error_free(err);
}

Test(filterx_func_parse_csv, test_optional_argument_dialect)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL,
                                                      filterx_literal_new(filterx_string_new("\"PTHREAD \\\"support initialized\"", -1))));
  args = g_list_append(args, filterx_function_arg_new("dialect",
                                                      filterx_literal_new(filterx_string_new("escape-backslash", -1))));

  GError *err = NULL;
  GError *args_err = NULL;
  FilterXExpr *func = _csv_new(filterx_function_args_new(args, &args_err), &err, filterx_json_array_new_empty());
  cr_assert_null(args_err);
  cr_assert_null(err);

  FilterXObject *obj = filterx_expr_eval(func);

  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(json_array)));

  GString *repr = scratch_buffers_alloc();

  LogMessageValueType lmvt;
  cr_assert(filterx_object_marshal(obj, repr, &lmvt));

  cr_assert_str_eq(repr->str, "'PTHREAD \"support initialized'");
  filterx_expr_unref(func);
  filterx_object_unref(obj);
  g_error_free(err);
}

Test(filterx_func_parse_csv, test_optional_argument_flag_greedy)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL,
                                                      filterx_literal_new(filterx_string_new("foo,bar,baz,tik,tak,toe", -1))));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_PARSE_CSV_ARG_NAME_COLUMNS,
                                                      filterx_literal_new(_generate_string_list("1st", "2nd",
                                                          "3rd", "rest", NULL)))); // columns
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_PARSE_CSV_ARG_NAME_GREEDY,
                                                      filterx_literal_new(filterx_boolean_new(TRUE)))); // greedy

  GError *err = NULL;
  GError *args_err = NULL;
  FilterXExpr *func = _csv_new(filterx_function_args_new(args, &args_err), &err, filterx_json_object_new_empty());
  cr_assert_null(args_err);
  cr_assert_null(err);

  FilterXObject *obj = filterx_expr_eval(func);

  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(json_object)));

  GString *repr = scratch_buffers_alloc();

  LogMessageValueType lmvt;
  cr_assert(filterx_object_marshal(obj, repr, &lmvt));

  cr_assert_str_eq(repr->str, "{\"1st\":\"foo\",\"2nd\":\"bar\",\"3rd\":\"baz\",\"rest\":\"tik,tak,toe\"}");
  filterx_expr_unref(func);
  filterx_object_unref(obj);
  g_error_free(err);
}

Test(filterx_func_parse_csv, test_optional_argument_flag_non_greedy)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL,
                                                      filterx_literal_new(filterx_string_new("foo,bar,baz,tik,tak,toe", -1))));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_PARSE_CSV_ARG_NAME_COLUMNS,
                                                      filterx_literal_new(_generate_string_list("1st", "2nd",
                                                          "3rd", "rest", NULL)))); // columns
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_PARSE_CSV_ARG_NAME_GREEDY,
                                                      filterx_literal_new(filterx_boolean_new(FALSE)))); // greedy

  GError *err = NULL;
  GError *args_err = NULL;
  FilterXExpr *func = _csv_new(filterx_function_args_new(args, &args_err), &err, filterx_json_object_new_empty());
  cr_assert_null(args_err);
  cr_assert_null(err);

  FilterXObject *obj = filterx_expr_eval(func);

  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(json_object)));

  GString *repr = scratch_buffers_alloc();

  LogMessageValueType lmvt;
  cr_assert(filterx_object_marshal(obj, repr, &lmvt));

  cr_assert_str_eq(repr->str, "{\"1st\":\"foo\",\"2nd\":\"bar\",\"3rd\":\"baz\",\"rest\":\"tik\"}");
  filterx_expr_unref(func);
  filterx_object_unref(obj);
  g_error_free(err);
}

Test(filterx_func_parse_csv, test_optional_argument_flag_strip_whitespace)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL,
                                                      filterx_literal_new(filterx_string_new("  foo ,    bar  , baz   ,    tik tak toe", -1))));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_PARSE_CSV_ARG_NAME_DELIMITER,
                                                      filterx_literal_new(filterx_string_new(",",
                                                          -1)))); // delimiter
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_PARSE_CSV_ARG_NAME_STRIP_WHITESPACES,
                                                      filterx_literal_new(filterx_boolean_new(TRUE)))); // strip_whitespace

  GError *err = NULL;
  GError *args_err = NULL;
  FilterXExpr *func = _csv_new(filterx_function_args_new(args, &args_err), &err, filterx_json_array_new_empty());
  cr_assert_null(args_err);
  cr_assert_null(err);

  FilterXObject *obj = filterx_expr_eval(func);

  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(json_array)));

  GString *repr = scratch_buffers_alloc();

  LogMessageValueType lmvt;
  cr_assert(filterx_object_marshal(obj, repr, &lmvt));

  cr_assert_str_eq(repr->str, "foo,bar,baz,\"tik tak toe\"");
  filterx_expr_unref(func);
  filterx_object_unref(obj);
  g_error_free(err);
}

Test(filterx_func_parse_csv, test_optional_argument_flag_not_to_strip_whitespace)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL,
                                                      filterx_literal_new(filterx_string_new("  foo ,    bar  , baz   ,    tik tak toe", -1))));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_PARSE_CSV_ARG_NAME_DELIMITER,
                                                      filterx_literal_new(filterx_string_new(",",
                                                          -1)))); // delimiter
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_PARSE_CSV_ARG_NAME_STRIP_WHITESPACES,
                                                      filterx_literal_new(filterx_boolean_new(FALSE)))); // strip_whitespace

  GError *err = NULL;
  GError *args_err = NULL;
  FilterXExpr *func = _csv_new(filterx_function_args_new(args, &args_err), &err, filterx_json_array_new_empty());
  cr_assert_null(args_err);
  cr_assert_null(err);

  FilterXObject *obj = filterx_expr_eval(func);

  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(json_array)));

  GString *repr = scratch_buffers_alloc();

  LogMessageValueType lmvt;
  cr_assert(filterx_object_marshal(obj, repr, &lmvt));

  cr_assert_str_eq(repr->str, "\"  foo \",\"    bar  \",\" baz   \",\"    tik tak toe\"");
  filterx_expr_unref(func);
  filterx_object_unref(obj);
  g_error_free(err);
}

Test(filterx_func_parse_csv, test_optional_argument_string_delimiters)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL,
                                                      filterx_literal_new(filterx_string_new("testingfoostringbardelimitersbazthisfooway", -1))));
  FilterXObject *string_delimiters = _generate_string_list("foo", "bar", "baz", NULL);
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_PARSE_CSV_ARG_NAME_STRING_DELIMITERS,
                                                      filterx_literal_new(string_delimiters)));

  GError *err = NULL;
  GError *args_err = NULL;
  FilterXExpr *func = _csv_new(filterx_function_args_new(args, &args_err), &err, filterx_json_array_new_empty());
  cr_assert_null(args_err);
  cr_assert_null(err);

  FilterXObject *obj = filterx_expr_eval(func);

  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(json_array)));

  GString *repr = scratch_buffers_alloc();

  LogMessageValueType lmvt;
  cr_assert(filterx_object_marshal(obj, repr, &lmvt));

  cr_assert_str_eq(repr->str, "testing,string,delimiters,this,way");
  filterx_expr_unref(func);
  filterx_object_unref(obj);
  g_error_free(err);
}

Test(filterx_func_parse_csv, test_optional_argument_string_delimiters_and_delimiters)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL,
                                                      filterx_literal_new(filterx_string_new("testing;delimiterfoochaos,withbarthis.longbazstring", -1))));
  FilterXObject *string_delimiters = _generate_string_list("foo", "bar", "baz", NULL);
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_PARSE_CSV_ARG_NAME_STRING_DELIMITERS,
                                                      filterx_literal_new(string_delimiters)));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_PARSE_CSV_ARG_NAME_DELIMITER,
                                                      filterx_literal_new(filterx_string_new(".,;", -1))));

  GError *err = NULL;
  GError *args_err = NULL;
  FilterXExpr *func = _csv_new(filterx_function_args_new(args, &args_err), &err, filterx_json_array_new_empty());
  cr_assert_null(args_err);
  cr_assert_null(err);

  FilterXObject *obj = filterx_expr_eval(func);

  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(json_array)));

  GString *repr = scratch_buffers_alloc();

  LogMessageValueType lmvt;
  cr_assert(filterx_object_marshal(obj, repr, &lmvt));

  cr_assert_str_eq(repr->str, "testing,delimiter,chaos,with,this,long,string");
  filterx_expr_unref(func);
  filterx_object_unref(obj);
  g_error_free(err);
}

Test(filterx_func_parse_csv, test_optional_argument_delimiter_default_unset_when_string_delimiters_set)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL,
                                                      filterx_literal_new(filterx_string_new("testfoobar,baz", -1))));
  FilterXObject *string_delimiters = _generate_string_list("foo", NULL);
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_PARSE_CSV_ARG_NAME_STRING_DELIMITERS,
                                                      filterx_literal_new(string_delimiters)));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_PARSE_CSV_ARG_NAME_DELIMITER,
                                                      filterx_literal_new(filterx_string_new("", -1))));

  GError *err = NULL;
  GError *args_err = NULL;
  FilterXExpr *func = _csv_new(filterx_function_args_new(args, &args_err), &err, filterx_json_array_new_empty());
  cr_assert_null(args_err);
  cr_assert_null(err);

  FilterXObject *obj = filterx_expr_eval(func);

  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(json_array)));

  FilterXObject *elt = filterx_list_get_subscript(obj, 1);

  GString *repr = scratch_buffers_alloc();
  cr_assert(filterx_object_repr(elt, repr));

  cr_assert_str_eq(repr->str, "bar,baz");
  filterx_object_unref(elt);
  filterx_expr_unref(func);
  filterx_object_unref(obj);
  g_error_free(err);
}

Test(filterx_func_parse_csv, test_optional_argument_delimiter_unable_to_set_with_empty_string_delimiters)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL,
                                                      filterx_literal_new(filterx_string_new("testfoobar,baz", -1))));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_PARSE_CSV_ARG_NAME_DELIMITER,
                                                      filterx_literal_new(filterx_string_new("", -1))));

  GError *err = NULL;
  GError *args_err = NULL;
  FilterXExpr *func = _csv_new(filterx_function_args_new(args, &args_err), &err, filterx_json_array_new_empty());
  cr_assert_null(args_err);
  cr_assert_not_null(err);
  cr_assert(strcmp(err->message, FILTERX_FUNC_PARSE_ERR_EMPTY_DELIMITER));

  filterx_expr_unref(func);
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

TestSuite(filterx_func_parse_csv, .init = setup, .fini = teardown);
