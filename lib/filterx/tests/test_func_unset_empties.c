/*
 * Copyright (c) 2024 Attila Szakacs
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

#include "filterx/func-unset-empties.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "filterx/object-dict.h"
#include "filterx/object-list.h"
#include "filterx/json-repr.h"
#include "filterx/expr-literal.h"
#include "filterx/expr-literal-container.h"
#include "filterx/object-null.h"

#include "apphook.h"
#include "scratch-buffers.h"

static void
_assert_unset_empties_init_fail(GList *args)
{
  GError *err = NULL;
  GError *args_err = NULL;
  FilterXExpr *func = filterx_function_unset_empties_new(filterx_function_args_new(args, &args_err), &err);
  cr_assert(!func);
  cr_assert(err);
  g_error_free(err);
}

static void
_assert_unset_empties(GList *args, const gchar *expected_repr)
{
  FilterXExpr *modifiable_object_expr = filterx_expr_ref(((FilterXFunctionArg *) args->data)->value);

  GError *err = NULL;
  GError *args_err = NULL;
  FilterXExpr *func = filterx_function_unset_empties_new(filterx_function_args_new(args, &args_err), &err);
  cr_assert(!err);

  FilterXObject *obj = init_and_eval_expr(func);
  cr_assert(obj);
  gboolean success;
  cr_assert(filterx_boolean_unwrap(obj, &success));
  cr_assert(success);

  FilterXObject *modifiable_object = init_and_eval_expr(modifiable_object_expr);
  cr_assert(modifiable_object);

  assert_object_repr_equals(modifiable_object, expected_repr);

  filterx_object_unref(obj);
  filterx_expr_unref(func);
  filterx_object_unref(modifiable_object);
  filterx_expr_unref(modifiable_object_expr);
}

FilterXExpr *
_list_generator_helper(FilterXObject *first, ...)
{
  GList *target_vals = NULL;
  va_list va;

  va_start(va, first);
  FilterXObject *arg = first;
  while (arg)
    {
      target_vals = g_list_append(target_vals, filterx_literal_element_new(NULL, filterx_literal_new(arg)));
      arg = va_arg(va, FilterXObject *);
    }
  va_end(va);

  return filterx_literal_list_new(target_vals);
}

Test(filterx_func_unset_empties, invalid_args)
{
  GList *args = NULL;

  /* no args */
  _assert_unset_empties_init_fail(NULL);

  /* non-literal recursive */
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_test_dict_new())));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_UNSET_EMPTIES_ARG_NAME_RECURSIVE,
                                                      filterx_non_literal_new(filterx_boolean_new(TRUE))));
  _assert_unset_empties_init_fail(args);
  args = NULL;

  /* non-boolean recursive */
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_test_dict_new())));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_UNSET_EMPTIES_ARG_NAME_RECURSIVE,
                                                      filterx_literal_new(filterx_string_new("", -1))));
  _assert_unset_empties_init_fail(args);
  args = NULL;
}

Test(filterx_func_unset_empties, default_empties)
{
  const gchar *input = "[\"\", null, [], {}]";
  GList *args = g_list_append(NULL, filterx_function_arg_new(NULL,
                                                             filterx_literal_new(filterx_object_from_json(input, -1, NULL))));
  _assert_unset_empties(args, "[]");
}

Test(filterx_func_unset_empties, recursive)
{
  GList *args = NULL;

  /* dict */

  /* default is true */
  args = g_list_append(args, filterx_function_arg_new(NULL,
                                                      filterx_literal_new(filterx_object_from_json("[{\"foo\":\"\"}]", -1, NULL))));
  _assert_unset_empties(args, "[]");
  args = NULL;

  args = g_list_append(args, filterx_function_arg_new(NULL,
                                                      filterx_literal_new(filterx_object_from_json("[{\"foo\":\"\"}]", -1, NULL))));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_UNSET_EMPTIES_ARG_NAME_RECURSIVE,
                                                      filterx_literal_new(filterx_boolean_new(FALSE))));
  _assert_unset_empties(args, "[{\"foo\":\"\"}]");
  args = NULL;

  /* list */

  /* default is true */
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_object_from_json("[[\"\"]]",
                                                      -1, NULL))));
  _assert_unset_empties(args, "[]");
  args = NULL;

  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_object_from_json("[[\"\"]]",
                                                      -1, NULL))));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_UNSET_EMPTIES_ARG_NAME_RECURSIVE,
                                                      filterx_literal_new(filterx_boolean_new(FALSE))));
  _assert_unset_empties(args, "[[\"\"]]");
  args = NULL;
}

Test(filterx_func_unset_empties, target_resets_defaults)
{
  const gchar *input = "[\"\",null,[],{}]";
  GList *args = g_list_append(NULL, filterx_function_arg_new(NULL,
                                                             filterx_literal_new(filterx_object_from_json(input, -1, NULL))));
  FilterXExpr *targets = _list_generator_helper(filterx_string_new("anything", -1), NULL);
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_UNSET_EMPTIES_ARG_NAME_TARGETS,
                                                      targets));
  _assert_unset_empties(args, input);
}

Test(filterx_func_unset_empties, target_null_only)
{
  const gchar *input = "[\"\",null,[],{}]";
  GList *args = g_list_append(NULL, filterx_function_arg_new(NULL,
                                                             filterx_literal_new(filterx_object_from_json(input, -1, NULL))));
  FilterXExpr *targets = _list_generator_helper(filterx_null_new(), NULL);
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_UNSET_EMPTIES_ARG_NAME_TARGETS,
                                                      targets));
  _assert_unset_empties(args, "[\"\",[],{}]");
}

Test(filterx_func_unset_empties, target_empty_string_only)
{
  const gchar *input = "[\"\",null,[],{}]";
  GList *args = g_list_append(NULL, filterx_function_arg_new(NULL,
                                                             filterx_literal_new(filterx_object_from_json(input, -1, NULL))));
  FilterXExpr *targets = _list_generator_helper(filterx_string_new("", -1), NULL);
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_UNSET_EMPTIES_ARG_NAME_TARGETS,
                                                      targets));
  _assert_unset_empties(args, "[null,[],{}]");
}

Test(filterx_func_unset_empties, target_empty_list_only)
{
  const gchar *input = "[\"\",null,[],{}]";
  GList *args = g_list_append(NULL, filterx_function_arg_new(NULL,
                                                             filterx_literal_new(filterx_object_from_json(input, -1, NULL))));
  FilterXExpr *targets = _list_generator_helper(filterx_list_new(), NULL);
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_UNSET_EMPTIES_ARG_NAME_TARGETS,
                                                      targets));
  _assert_unset_empties(args, "[\"\",null,{}]");
}

Test(filterx_func_unset_empties, target_empty_dict_only)
{
  const gchar *input = "[\"\",null,[],{}]";
  GList *args = g_list_append(NULL, filterx_function_arg_new(NULL,
                                                             filterx_literal_new(filterx_object_from_json(input, -1, NULL))));
  FilterXExpr *targets = _list_generator_helper(filterx_dict_new(), NULL);
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_UNSET_EMPTIES_ARG_NAME_TARGETS,
                                                      targets));
  _assert_unset_empties(args, "[\"\",null,[]]");
}

Test(filterx_func_unset_empties, target_empties_manual)
{
  const gchar *input = "[\"\",null,[],{}]";
  GList *args = g_list_append(NULL, filterx_function_arg_new(NULL,
                                                             filterx_literal_new(filterx_object_from_json(input, -1, NULL))));
  FilterXExpr *targets = _list_generator_helper(filterx_dict_new(),
                                                filterx_list_new(),
                                                filterx_string_new("", -1),
                                                filterx_null_new(), NULL);
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_UNSET_EMPTIES_ARG_NAME_TARGETS,
                                                      targets));
  _assert_unset_empties(args, "[]");
}

Test(filterx_func_unset_empties, target_empties_manual_and_strings)
{
  const gchar *input = "[\"bar\",\"\",null,[],{},\"foo\",\"bar\",\"baz\"]";
  GList *args = g_list_append(NULL, filterx_function_arg_new(NULL,
                                                             filterx_literal_new(filterx_object_from_json(input, -1, NULL))));
  FilterXExpr *targets = _list_generator_helper(filterx_dict_new(),
                                                filterx_list_new(),
                                                filterx_string_new("", -1),
                                                filterx_null_new(),
                                                filterx_string_new("foo", -1),
                                                filterx_string_new("bar", -1),
                                                NULL);
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_UNSET_EMPTIES_ARG_NAME_TARGETS,
                                                      targets));
  _assert_unset_empties(args, "[\"baz\"]");
}

Test(filterx_func_unset_empties, string_targets)
{
  GList *args = NULL;
  FilterXExpr *targets = NULL;

  /* dict */
  targets = _list_generator_helper(filterx_string_new("baz", -1),
                                   NULL);
  args = g_list_append(args, filterx_function_arg_new(NULL,
                                                      filterx_literal_new(filterx_object_from_json("{\"foo\":{\"bar\":\"baz\",\"tik\":\"tak\"}}", -1, NULL))));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_UNSET_EMPTIES_ARG_NAME_TARGETS,
                                                      targets));
  _assert_unset_empties(args, "{\"foo\":{\"tik\":\"tak\"}}");
  args = NULL;


  targets = _list_generator_helper(filterx_string_new("baz", -1),
                                   filterx_string_new("tak", -1),
                                   filterx_dict_new(), //also remove empty dict
                                   NULL);
  args = g_list_append(args, filterx_function_arg_new(NULL,
                                                      filterx_literal_new(filterx_object_from_json("{\"foo\":{\"bar\":\"baz\",\"tik\":\"tak\"}}", -1, NULL))));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_UNSET_EMPTIES_ARG_NAME_TARGETS,
                                                      targets));
  _assert_unset_empties(args, "{}");
  args = NULL;

  // /* list */

  targets = _list_generator_helper(filterx_string_new("baz", -1),
                                   filterx_null_new(), // also remove null values
                                   NULL);
  args = g_list_append(args, filterx_function_arg_new(NULL,
                                                      filterx_literal_new(filterx_object_from_json("[\"foo\",\"bar\",null,\"baz\"]",
                                                          -1, NULL))));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_UNSET_EMPTIES_ARG_NAME_TARGETS,
                                                      targets));
  _assert_unset_empties(args, "[\"foo\",\"bar\"]");
  args = NULL;


  targets = _list_generator_helper(filterx_string_new("baz", -1),
                                   filterx_string_new("foo", -1),
                                   filterx_null_new(), // also remove null values
                                   NULL);
  args = g_list_append(args, filterx_function_arg_new(NULL,
                                                      filterx_literal_new(filterx_object_from_json("[\"foo\",\"bar\",null,\"baz\"]",
                                                          -1, NULL))));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_UNSET_EMPTIES_ARG_NAME_TARGETS,
                                                      targets));
  _assert_unset_empties(args, "[\"bar\"]");
  args = NULL;
}

Test(filterx_func_unset_empties, replacement)
{
  GList *args = NULL;
  FilterXExpr *targets = NULL;

  /* dict */

  targets = _list_generator_helper(filterx_string_new("baz", -1),
                                   filterx_string_new("tak", -1),
                                   NULL);
  args = g_list_append(args, filterx_function_arg_new(NULL,
                                                      filterx_literal_new(filterx_object_from_json("{\"foo\":{\"bar\":\"baz\",\"tik\":\"tak\"}}", -1, NULL))));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_UNSET_EMPTIES_ARG_NAME_TARGETS,
                                                      targets));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_UNSET_EMPTIES_ARG_NAME_REPLACEMENT,
                                                      filterx_literal_new(filterx_string_new("replaced", -1))));
  _assert_unset_empties(args, "{\"foo\":{\"bar\":\"replaced\",\"tik\":\"replaced\"}}");
  args = NULL;

  /* list */

  targets = _list_generator_helper(filterx_string_new("baz", -1),
                                   filterx_string_new("foo", -1),
                                   filterx_null_new(), //also replace null
                                   NULL);
  args = g_list_append(args, filterx_function_arg_new(NULL,
                                                      filterx_literal_new(filterx_object_from_json("[\"foo\",\"bar\",null,\"baz\"]",
                                                          -1, NULL))));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_UNSET_EMPTIES_ARG_NAME_TARGETS,
                                                      targets));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_UNSET_EMPTIES_ARG_NAME_REPLACEMENT,
                                                      filterx_literal_new(filterx_string_new("replaced", -1))));
  _assert_unset_empties(args, "[\"replaced\",\"bar\",\"replaced\",\"replaced\"]");
  args = NULL;
}

Test(filterx_func_unset_empties, ignorecase)
{
  GList *args = NULL;
  FilterXExpr *targets = NULL;

  /* dict */

  targets = _list_generator_helper(filterx_string_new("BAZ", -1),
                                   NULL);
  args = g_list_append(args, filterx_function_arg_new(NULL,
                                                      filterx_literal_new(filterx_object_from_json("{\"foo\":{\"bar\":\"baz\",\"tik\":\"tak\"}}", -1, NULL))));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_UNSET_EMPTIES_ARG_NAME_TARGETS,
                                                      targets));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_UNSET_EMPTIES_ARG_NAME_IGNORECASE,
                                                      filterx_literal_new(filterx_boolean_new(FALSE))));
  _assert_unset_empties(args, "{\"foo\":{\"bar\":\"baz\",\"tik\":\"tak\"}}");
  args = NULL;


  targets = _list_generator_helper(filterx_string_new("BAZ", -1),
                                   NULL);
  args = g_list_append(args, filterx_function_arg_new(NULL,
                                                      filterx_literal_new(filterx_object_from_json("{\"foo\":{\"bar\":\"baz\",\"tik\":\"tak\"}}", -1, NULL))));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_UNSET_EMPTIES_ARG_NAME_TARGETS,
                                                      targets));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_UNSET_EMPTIES_ARG_NAME_IGNORECASE,
                                                      filterx_literal_new(filterx_boolean_new(TRUE))));
  _assert_unset_empties(args, "{\"foo\":{\"tik\":\"tak\"}}");
  args = NULL;

  /* list */

  targets = _list_generator_helper(filterx_string_new("BAR", -1),
                                   filterx_null_new(), // also remove null
                                   NULL);
  args = g_list_append(args, filterx_function_arg_new(NULL,
                                                      filterx_literal_new(filterx_object_from_json("[\"foo\",\"bar\",null,\"baz\"]",
                                                          -1, NULL))));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_UNSET_EMPTIES_ARG_NAME_TARGETS,
                                                      targets));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_UNSET_EMPTIES_ARG_NAME_IGNORECASE,
                                                      filterx_literal_new(filterx_boolean_new(FALSE))));
  _assert_unset_empties(args, "[\"foo\",\"bar\",\"baz\"]");
  args = NULL;

  targets = _list_generator_helper(filterx_string_new("BAR", -1),
                                   filterx_null_new(), // also remove null
                                   NULL);
  args = g_list_append(args, filterx_function_arg_new(NULL,
                                                      filterx_literal_new(filterx_object_from_json("[\"foo\",\"bar\",null,\"baz\"]",
                                                          -1, NULL))));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_UNSET_EMPTIES_ARG_NAME_TARGETS,
                                                      targets));
  args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_UNSET_EMPTIES_ARG_NAME_IGNORECASE,
                                                      filterx_literal_new(filterx_boolean_new(TRUE))));
  _assert_unset_empties(args, "[\"foo\",\"baz\"]");
  args = NULL;

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

TestSuite(filterx_func_unset_empties, .init = setup, .fini = teardown);
