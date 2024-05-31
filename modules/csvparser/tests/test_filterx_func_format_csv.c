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

#include "filterx-func-format-csv.h"
#include "filterx/expr-literal.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "filterx/object-null.h"
#include "filterx/object-json.h"

#include "apphook.h"
#include "scratch-buffers.h"

static void
_assert_format_csv_init_fail(GList *args)
{
  GError *err = NULL;
  GError *args_err = NULL;
  FilterXFunction *func = filterx_function_format_csv_new("test", filterx_function_args_new(args, &args_err), &err);
  cr_assert(!func);
  cr_assert(err);
  g_error_free(err);
}

static void
_assert_format_csv(GList *args, const gchar *expected_output)
{
  GError *err = NULL;
  GError *args_err = NULL;
  FilterXFunction *func = filterx_function_format_csv_new("test", filterx_function_args_new(args, &args_err), &err);
  cr_assert(!err);

  FilterXObject *obj = filterx_expr_eval(&func->super);
  cr_assert(obj);

  const gchar *output = filterx_string_get_value(obj, NULL);
  cr_assert(output);

  cr_assert_str_eq(output, expected_output);

  filterx_object_unref(obj);
  filterx_expr_unref(&func->super);
}

Test(filterx_func_format_csv, test_invalid_args)
{
  GList *args = NULL;

  /* no args */
  _assert_format_csv_init_fail(NULL);

  /* empty args */
  args = g_list_append(args, filterx_function_arg_new(NULL, NULL));
  _assert_format_csv_init_fail(args);
  args = NULL;

  /* non literal delimiter */
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_test_dict_new())));
  args = g_list_append(args, filterx_function_arg_new("delimiter", filterx_non_literal_new(filterx_string_new(",",
                                                      -1))));
  _assert_format_csv_init_fail(args);
  args = NULL;

  /* too long delimiter */
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_test_dict_new())));
  args = g_list_append(args, filterx_function_arg_new("delimiter", filterx_literal_new(filterx_string_new(",!@#$",
                                                      -1))));
  _assert_format_csv_init_fail(args);
  args = NULL;

  /* too_many_args */
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_test_dict_new())));
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new("=", -1))));
  _assert_format_csv_init_fail(args);
  args = NULL;
}

Test(filterx_func_format_csv, test_array_mode_with_default_delimiter)
{
  FilterXExpr *csv_data = filterx_literal_new(filterx_json_array_new_from_repr("[\"foo\", \"bar\", \"baz\"]", -1));
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, csv_data));

  _assert_format_csv(args, "foo bar baz");
}

Test(filterx_func_format_csv, test_array_mode_with_custom_delimiter)
{
  FilterXExpr *csv_data = filterx_literal_new(filterx_json_array_new_from_repr("[\"foo\", \"bar\", \"baz\"]", -1));
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, csv_data));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_FORMAT_CSV_ARG_NAME_DELIMITER,
                                                      filterx_literal_new(filterx_string_new(";", -1))));

  _assert_format_csv(args, "foo;bar;baz");
}

Test(filterx_func_format_csv, test_array_mode_skip_column_names)
{
  FilterXExpr *csv_data = filterx_literal_new(filterx_json_array_new_from_repr("[\"foo\", \"bar\", \"baz\"]", -1));
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, csv_data));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_FORMAT_CSV_ARG_NAME_COLUMNS,
                                                      filterx_non_literal_new(filterx_json_array_new_from_repr("[\"col1\",\"col2\"]", -1))));

  _assert_format_csv(args, "foo bar baz");
}

Test(filterx_func_format_csv, test_dict_mode_without_column_names_with_default_delimiter)
{
  FilterXExpr *csv_data = filterx_literal_new(
                            filterx_json_object_new_from_repr("{\"col1\":\"foo\",\"col2\":\"bar\",\"col3\":\"baz\"}", -1));
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, csv_data));

  _assert_format_csv(args, "foo bar baz");
}

Test(filterx_func_format_csv, test_dict_mode_without_column_names_with_custom_delimiter)
{
  FilterXExpr *csv_data = filterx_literal_new(
                            filterx_json_object_new_from_repr("{\"col1\":\"foo\",\"col2\":\"bar\",\"col3\":\"baz\"}", -1));
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, csv_data));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_FORMAT_CSV_ARG_NAME_DELIMITER,
                                                      filterx_literal_new(filterx_string_new(";", -1))));

  _assert_format_csv(args, "foo;bar;baz");
}

Test(filterx_func_format_csv, test_dict_mode_with_column_names_with_default_delimiter)
{
  FilterXExpr *csv_data = filterx_literal_new(
                            filterx_json_object_new_from_repr("{\"col1\":\"foo\",\"col2\":\"bar\",\"col3\":\"baz\"}", -1));
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, csv_data));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_FORMAT_CSV_ARG_NAME_COLUMNS,
                                                      filterx_non_literal_new(filterx_json_array_new_from_repr("[\"col2\",\"col1\"]", -1))));

  _assert_format_csv(args, "bar foo");
}

Test(filterx_func_format_csv, test_dict_mode_with_column_names_with_custom_delimiter)
{
  FilterXExpr *csv_data = filterx_literal_new(
                            filterx_json_object_new_from_repr("{\"col1\":\"foo\",\"col2\":\"bar\",\"col3\":\"baz\"}", -1));
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, csv_data));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_FORMAT_CSV_ARG_NAME_DELIMITER,
                                                      filterx_literal_new(filterx_string_new(";", -1))));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_FORMAT_CSV_ARG_NAME_COLUMNS,
                                                      filterx_non_literal_new(filterx_json_array_new_from_repr("[\"col3\",\"col1\"]", -1))));

  _assert_format_csv(args, "baz;foo");
}

Test(filterx_func_format_csv, test_add_double_quotes_when_delimiter_character_occours_in_data)
{
  FilterXExpr *csv_data = filterx_literal_new(
                            filterx_json_object_new_from_repr("{\"col1\":\"fo;o\",\"col2\":\"b;ar\",\"col3\":\"baz\"}", -1));
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, csv_data));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_FORMAT_CSV_ARG_NAME_DELIMITER,
                                                      filterx_literal_new(filterx_string_new(";", -1))));

  // foo and bar gets double qouted since they contain delimiter character
  _assert_format_csv(args, "\"fo;o\";\"b;ar\";baz");
}

Test(filterx_func_format_csv, test_escape_existing_double_quotes_in_case_of_adding_double_quotes)
{
  FilterXExpr *csv_data = filterx_literal_new(
                            filterx_json_object_new_from_repr("{\"col1\":\"\\\"fo;o\\\"\",\"col2\":\"b;ar\",\"col3\":\"\\\"baz\\\"\"}", -1));
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, csv_data));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_FORMAT_CSV_ARG_NAME_DELIMITER,
                                                      filterx_literal_new(filterx_string_new(";", -1))));

  // foo's ("fo;o") double quotes need to be escaped, since it contains delimiter character and gets double quoted
  // baz's ("baz") dobule quotes remain intact since won't be double quoted by the formatter
  _assert_format_csv(args, "\"\\\"fo;o\\\"\";\"b;ar\";\"baz\"");
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

TestSuite(filterx_func_format_csv, .init = setup, .fini = teardown);
