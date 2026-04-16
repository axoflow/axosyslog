/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#include <criterion/criterion.h>
#include "libtest/filterx-lib.h"

#include "filterx/func-glob.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "filterx/object-list.h"
#include "filterx/expr-function.h"
#include "filterx/expr-literal.h"
#include "filterx/filterx-sequence.h"
#include "filterx/filterx-private.h"

#include "apphook.h"
#include "scratch-buffers.h"

static FilterXExpr *
_create_glob_match_expr(const gchar *filename, const gchar *patterns[], gsize num_patterns)
{
  FilterXObject *list = filterx_list_new();
  for (gsize i = 0; i < num_patterns; i++)
    {
      FilterXObject *pattern = filterx_string_new(patterns[i], -1);
      filterx_sequence_append(list, &pattern);
      filterx_object_unref(pattern);
    }

  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new(filename, -1))));
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(list)));

  GError *error = NULL;
  FilterXExpr *fn = filterx_simple_function_new("glob_match", filterx_function_args_new(args, NULL),
                                                filterx_simple_function_glob_match, &error);
  cr_assert_null(error);
  return fn;
}

static gboolean
_eval_glob_match(const gchar *filename, const gchar *patterns[], gsize num_patterns)
{
  FilterXExpr *fn = _create_glob_match_expr(filename, patterns, num_patterns);
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_not_null(res);

  gboolean result;
  cr_assert(filterx_boolean_unwrap(res, &result));

  filterx_object_unref(res);
  filterx_expr_unref(fn);
  return result;
}

Test(filterx_func_glob_match, single_pattern_matches)
{
  const gchar *patterns[] = { "*.log" };
  cr_assert(_eval_glob_match("filename.log", patterns, G_N_ELEMENTS(patterns)));
}

Test(filterx_func_glob_match, single_pattern_does_not_match)
{
  const gchar *patterns[] = { "*.log" };
  cr_assert_not(_eval_glob_match("filename.txt", patterns, G_N_ELEMENTS(patterns)));
}

Test(filterx_func_glob_match, question_mark_wildcard)
{
  const gchar *patterns[] = { "file.???" };
  cr_assert(_eval_glob_match("file.txt", patterns, G_N_ELEMENTS(patterns)));
  cr_assert_not(_eval_glob_match("file.py", patterns, G_N_ELEMENTS(patterns)));
}

Test(filterx_func_glob_match, path_pattern)
{
  const gchar *patterns[] = { "/var/log/*" };
  cr_assert(_eval_glob_match("/var/log/syslog", patterns, G_N_ELEMENTS(patterns)));
  cr_assert_not(_eval_glob_match("/var/run/syslog", patterns, G_N_ELEMENTS(patterns)));
}

Test(filterx_func_glob_match, multiple_patterns_first_matches)
{
  const gchar *patterns[] = { "*.log", "*.txt" };
  cr_assert(_eval_glob_match("filename.log", patterns, G_N_ELEMENTS(patterns)));
}

Test(filterx_func_glob_match, multiple_patterns_second_matches)
{
  const gchar *patterns[] = { "*.log", "*.txt" };
  cr_assert(_eval_glob_match("filename.txt", patterns, G_N_ELEMENTS(patterns)));
}

Test(filterx_func_glob_match, multiple_patterns_none_match)
{
  const gchar *patterns[] = { "*.log", "*.txt" };
  cr_assert_not(_eval_glob_match("filename.cfg", patterns, G_N_ELEMENTS(patterns)));
}

Test(filterx_func_glob_match, empty_pattern_list_never_matches)
{
  cr_assert_not(_eval_glob_match("filename.log", NULL, 0));
}

Test(filterx_func_glob_match, rejects_no_arguments)
{
  GError *error = NULL;
  FilterXExpr *fn = filterx_simple_function_new("glob_match", filterx_function_args_new(NULL, NULL),
                                                filterx_simple_function_glob_match, &error);
  cr_assert_null(error);

  FilterXObject *res = init_and_eval_expr(fn);
  cr_assert_null(res);

  filterx_expr_unref(fn);
}

Test(filterx_func_glob_match, rejects_filename_only)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL,
                                                      filterx_literal_new(filterx_string_new("filename.log", -1))));

  GError *error = NULL;
  FilterXExpr *fn = filterx_simple_function_new("glob_match", filterx_function_args_new(args, NULL),
                                                filterx_simple_function_glob_match, &error);
  cr_assert_null(error);

  FilterXObject *res = init_and_eval_expr(fn);
  cr_assert_null(res);

  filterx_expr_unref(fn);
}

Test(filterx_func_glob_match, rejects_non_string_filename)
{
  FilterXObject *list = filterx_list_new();
  FilterXObject *pattern = filterx_string_new("*.log", -1);
  filterx_sequence_append(list, &pattern);
  filterx_object_unref(pattern);

  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_integer_new(42))));
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(list)));

  GError *error = NULL;
  FilterXExpr *fn = filterx_simple_function_new("glob_match", filterx_function_args_new(args, NULL),
                                                filterx_simple_function_glob_match, &error);
  cr_assert_null(error);

  FilterXObject *res = init_and_eval_expr(fn);
  cr_assert_null(res);

  filterx_expr_unref(fn);
}

Test(filterx_func_glob_match, rejects_non_string_pattern)
{
  FilterXObject *list = filterx_list_new();
  FilterXObject *pattern = filterx_integer_new(42);
  filterx_sequence_append(list, &pattern);
  filterx_object_unref(pattern);

  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL,
                                                      filterx_literal_new(filterx_string_new("filename.log", -1))));
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(list)));

  GError *error = NULL;
  FilterXExpr *fn = filterx_simple_function_new("glob_match", filterx_function_args_new(args, NULL),
                                                filterx_simple_function_glob_match, &error);
  cr_assert_null(error);

  FilterXObject *res = init_and_eval_expr(fn);
  cr_assert_null(res);

  filterx_expr_unref(fn);
}

Test(filterx_func_glob_match, rejects_non_list_or_string_patterns_arg)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL,
                                                      filterx_literal_new(filterx_string_new("filename.log", -1))));
  args = g_list_append(args, filterx_function_arg_new(NULL,
                                                      filterx_literal_new(filterx_integer_new(42))));

  GError *error = NULL;
  FilterXExpr *fn = filterx_simple_function_new("glob_match", filterx_function_args_new(args, NULL),
                                                filterx_simple_function_glob_match, &error);
  cr_assert_null(error);

  FilterXObject *res = init_and_eval_expr(fn);
  cr_assert_null(res);

  filterx_expr_unref(fn);
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

TestSuite(filterx_func_glob_match, .init = setup, .fini = teardown);
