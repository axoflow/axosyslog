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

#include <criterion/criterion.h>
#include "libtest/filterx-lib.h"

#include "filterx/expr-regexp-search.h"
#include "filterx/expr-literal.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "filterx/object-dict-interface.h"
#include "filterx/object-list-interface.h"
#include "filterx/filterx-object-istype.h"
#include "apphook.h"
#include "scratch-buffers.h"
#include "compat/pcre.h"

static void
_parse_search_flags(GList *args, FLAGSET flags)
{
  FUNC_FLAGS_ITER(FilterXRegexpSearchFlags,
  {
    if (check_flag(flags, enum_elt))
      {
        const gchar *flag_name = FilterXRegexpSearchFlags_NAMES[enum_elt];
        args = g_list_append(args, filterx_function_arg_new(flag_name, filterx_literal_new(filterx_boolean_new(TRUE))));
      }
  })
}

static FilterXObject *
_search(const gchar *lhs, const gchar *pattern, FLAGSET flags)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_non_literal_new(filterx_string_new(lhs, -1))));
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new(pattern, -1))));
  _parse_search_flags(args, flags);

  FilterXExpr *expr = filterx_generator_function_regexp_search_new(filterx_function_args_new(args, NULL), NULL);
  FilterXExpr *parent_fillable_expr_new = filterx_literal_new(filterx_test_dict_new());
  FilterXExpr *cc_expr = filterx_generator_create_container_new(expr, parent_fillable_expr_new);
  FilterXExpr *fillable_expr = filterx_literal_new(filterx_expr_eval(cc_expr));
  filterx_generator_set_fillable(expr, fillable_expr);

  FilterXObject *result_obj = filterx_expr_eval(expr);
  cr_assert(result_obj);
  cr_assert(filterx_object_truthy(result_obj));

  FilterXObject *fillable = filterx_expr_eval(fillable_expr);
  cr_assert(fillable);

  filterx_object_unref(result_obj);
  filterx_expr_unref(cc_expr);

  return fillable;
}

static void
_search_with_fillable(const gchar *lhs, const gchar *pattern, FilterXObject *fillable, FLAGSET flags)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_non_literal_new(filterx_string_new(lhs, -1))));
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new(pattern, -1))));
  _parse_search_flags(args, flags);

  FilterXExpr *expr = filterx_generator_function_regexp_search_new(filterx_function_args_new(args, NULL), NULL);
  filterx_generator_set_fillable(expr, filterx_literal_new(filterx_object_ref(fillable)));

  FilterXObject *result_obj = filterx_expr_eval(expr);
  cr_assert(result_obj);
  cr_assert(filterx_object_truthy(result_obj));

  filterx_object_unref(result_obj);
  filterx_expr_unref(expr);
}

static void
_assert_search_init_error(const gchar *lhs, const gchar *pattern)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_non_literal_new(filterx_string_new(lhs, -1))));
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new(pattern, -1))));

  GError *arg_err = NULL;
  GError *func_err = NULL;
  cr_assert_not(filterx_generator_function_regexp_search_new(filterx_function_args_new(args, &arg_err), &func_err));

  cr_assert(arg_err || func_err);
  g_clear_error(&arg_err);
  g_clear_error(&func_err);
}

static void
_assert_len(FilterXObject *obj, guint64 expected_len)
{
  guint64 len;
  cr_assert(filterx_object_len(obj, &len));
  cr_assert_eq(len, expected_len, "len mismatch. expected: %" G_GUINT64_FORMAT " actual: %" G_GUINT64_FORMAT,
               expected_len, len);
}

static void
_assert_list_elem(FilterXObject *list, gint64 index, const gchar *expected_value)
{
  FilterXObject *elem = filterx_list_get_subscript(list, index);
  cr_assert(elem);

  const gchar *value = filterx_string_get_value_ref(elem, NULL);
  cr_assert_str_eq(value, expected_value);

  filterx_object_unref(elem);
}

static void
_assert_dict_elem(FilterXObject *list, const gchar *key, const gchar *expected_value)
{
  FilterXObject *key_obj = filterx_string_new(key, -1);
  FilterXObject *elem = filterx_object_get_subscript(list, key_obj);
  cr_assert(elem);

  const gchar *value = filterx_string_get_value_ref(elem, NULL);
  cr_assert_str_eq(value, expected_value);

  filterx_object_unref(key_obj);
  filterx_object_unref(elem);
}

Test(filterx_expr_regexp_search, unnamed)
{
  FilterXObject *result = _search("foobarbaz", "(foo)(bar)(baz)", 0);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(dict)));
  _assert_len(result, 3);
  _assert_dict_elem(result, "1", "foo");
  _assert_dict_elem(result, "2", "bar");
  _assert_dict_elem(result, "3", "baz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_search, unnamed_grp_zero)
{
  FilterXObject *result = _search("foobarbaz", "(foo)(bar)(baz)", FLAG_VAL(FILTERX_REGEXP_SEARCH_KEEP_GRP_ZERO));
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(dict)));
  _assert_len(result, 4);
  _assert_dict_elem(result, "0", "foobarbaz");
  _assert_dict_elem(result, "1", "foo");
  _assert_dict_elem(result, "2", "bar");
  _assert_dict_elem(result, "3", "baz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_search, unnamed_grp_zero_list_mode)
{
  FilterXObject *result = _search("foobarbaz", "(foo)(bar)(baz)",
                                  FLAG_VAL(FILTERX_REGEXP_SEARCH_KEEP_GRP_ZERO) | FLAG_VAL(FILTERX_REGEXP_SEARCH_LIST_MODE));
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(list)));
  _assert_len(result, 4);
  _assert_list_elem(result, 0, "foobarbaz");
  _assert_list_elem(result, 1, "foo");
  _assert_list_elem(result, 2, "bar");
  _assert_list_elem(result, 3, "baz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_search, named)
{
  FilterXObject *result = _search("foobarbaz", "(?<first>foo)(?<second>bar)(?<third>baz)", 0);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(dict)));
  _assert_len(result, 3);
  _assert_dict_elem(result, "first", "foo");
  _assert_dict_elem(result, "second", "bar");
  _assert_dict_elem(result, "third", "baz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_search, named_grp_zero)
{
  FilterXObject *result = _search("foobarbaz", "(?<first>foo)(?<second>bar)(?<third>baz)",
                                  FLAG_VAL(FILTERX_REGEXP_SEARCH_KEEP_GRP_ZERO));
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(dict)));
  _assert_len(result, 4);
  _assert_dict_elem(result, "0", "foobarbaz");
  _assert_dict_elem(result, "first", "foo");
  _assert_dict_elem(result, "second", "bar");
  _assert_dict_elem(result, "third", "baz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_search, named_grp_zero_list_mode)
{
  FilterXObject *result = _search("foobarbaz", "(?<first>foo)(?<second>bar)(?<third>baz)",
                                  FLAG_VAL(FILTERX_REGEXP_SEARCH_KEEP_GRP_ZERO) | FLAG_VAL(FILTERX_REGEXP_SEARCH_LIST_MODE));
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(list)));
  _assert_len(result, 4);
  _assert_list_elem(result, 0, "foobarbaz");
  _assert_list_elem(result, 1, "foo");
  _assert_list_elem(result, 2, "bar");
  _assert_list_elem(result, 3, "baz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_search, mixed)
{
  FilterXObject *result = _search("foobarbaz", "(?<first>foo)(bar)(?<third>baz)", 0);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(dict)));
  _assert_len(result, 3);
  _assert_dict_elem(result, "first", "foo");
  _assert_dict_elem(result, "2", "bar");
  _assert_dict_elem(result, "third", "baz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_search, forced_list)
{
  FilterXObject *result = filterx_test_list_new();
  _search_with_fillable("foobarbaz", "(?<first>foo)(bar)(?<third>baz)", result, 0);
  _assert_len(result, 3);
  _assert_list_elem(result, 0, "foo");
  _assert_list_elem(result, 1, "bar");
  _assert_list_elem(result, 2, "baz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_search, forced_dict)
{
  FilterXObject *result = filterx_test_dict_new();
  _search_with_fillable("foobarbaz", "(foo)(bar)(baz)", result, 0);
  _assert_len(result, 3);
  _assert_dict_elem(result, "1", "foo");
  _assert_dict_elem(result, "2", "bar");
  _assert_dict_elem(result, "3", "baz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_search, forced_dict_list_mode)
{
  // list mode overrides the default dict container creation, but still returns dict when fillable type is forced
  FilterXObject *result = filterx_test_dict_new();
  _search_with_fillable("foobarbaz", "(foo)(bar)(baz)", result, FLAG_VAL(FILTERX_REGEXP_SEARCH_LIST_MODE));
  _assert_len(result, 3);
  _assert_dict_elem(result, "1", "foo");
  _assert_dict_elem(result, "2", "bar");
  _assert_dict_elem(result, "3", "baz");
  filterx_object_unref(result);
}


Test(filterx_expr_regexp_search, unnamed_no_match)
{
  FilterXObject *result = _search("foobarbaz", "(almafa)", 0);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(dict)));
  _assert_len(result, 0);
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_search, named_no_match)
{
  FilterXObject *result = _search("foobarbaz", "(?<first>almafa)", 0);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(dict)));
  _assert_len(result, 0);
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_search, retain_group_zero_if_sole)
{
  FilterXObject *result = _search("foobarbaz", "foobarbaz", 0);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(dict)));
  _assert_len(result, 1);
  _assert_dict_elem(result, "0", "foobarbaz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_search, retain_group_zero_if_sole_list_mode)
{
  FilterXObject *result = _search("foobarbaz", "foobarbaz", FLAG_VAL(FILTERX_REGEXP_SEARCH_LIST_MODE));
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(list)));
  _assert_len(result, 1);
  _assert_list_elem(result, 0, "foobarbaz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_search, init_error)
{
  _assert_search_init_error("foobarbaz", "(");
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

TestSuite(filterx_expr_regexp_search, .init = setup, .fini = teardown);
