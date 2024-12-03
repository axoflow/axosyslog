/*
 * Copyright (c) 2024 Attila Szakacs
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

#include "filterx/expr-regexp.h"
#include "filterx/expr-literal.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "filterx/object-dict-interface.h"
#include "filterx/object-list-interface.h"
#include "filterx/filterx-object-istype.h"
#include "apphook.h"
#include "scratch-buffers.h"
#include "compat/pcre.h"

static gboolean
_check_match(const gchar *lhs, const gchar *pattern)
{
  FilterXExpr *expr = filterx_expr_regexp_match_new(filterx_literal_new(filterx_string_new(lhs, -1)), pattern);

  FilterXObject *result_obj = filterx_expr_eval(expr);
  cr_assert(result_obj);
  gboolean result;
  cr_assert(filterx_boolean_unwrap(result_obj, &result));

  filterx_expr_unref(expr);
  filterx_object_unref(result_obj);

  return result;
}

static void
_assert_match(const gchar *lhs, const gchar *pattern)
{
  cr_assert(_check_match(lhs, pattern), "regexp should match. lhs: %s pattern: %s", lhs, pattern);
}

static void
_assert_not_match(const gchar *lhs, const gchar *pattern)
{
  cr_assert_not(_check_match(lhs, pattern), "regexp should not match. lhs: %s pattern: %s", lhs, pattern);
}

static void
_assert_match_init_error(const gchar *lhs, const gchar *pattern)
{
  cr_assert_not(filterx_expr_regexp_match_new(filterx_literal_new(filterx_string_new(lhs, -1)), pattern));
}

Test(filterx_expr_regexp, regexp_match)
{
  _assert_match("foo", "(?<key>foo)");
  _assert_match("foo", "(?<key>foo)|(?<key>bar)");
  _assert_not_match("abc", "Abc");
  _assert_not_match("abc", "(?<key>Abc)");
  _assert_match_init_error("abc", "(");
}

static FilterXObject *
_search(const gchar *lhs, const gchar *pattern)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_non_literal_new(filterx_string_new(lhs, -1))));
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new(pattern, -1))));

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
_search_with_fillable(const gchar *lhs, const gchar *pattern, FilterXObject *fillable)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_non_literal_new(filterx_string_new(lhs, -1))));
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new(pattern, -1))));

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

Test(filterx_expr_regexp, regexp_search_unnamed)
{
  FilterXObject *result = _search("foobarbaz", "(foo)(bar)(baz)");
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(list)));
  _assert_len(result, 4);
  _assert_list_elem(result, 0, "foobarbaz");
  _assert_list_elem(result, 1, "foo");
  _assert_list_elem(result, 2, "bar");
  _assert_list_elem(result, 3, "baz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp, regexp_search_named)
{
  FilterXObject *result = _search("foobarbaz", "(?<first>foo)(?<second>bar)(?<third>baz)");
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(dict)));
  _assert_len(result, 4);
  _assert_dict_elem(result, "0", "foobarbaz");
  _assert_dict_elem(result, "first", "foo");
  _assert_dict_elem(result, "second", "bar");
  _assert_dict_elem(result, "third", "baz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp, regexp_search_mixed)
{
  FilterXObject *result = _search("foobarbaz", "(?<first>foo)(bar)(?<third>baz)");
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(dict)));
  _assert_len(result, 4);
  _assert_dict_elem(result, "0", "foobarbaz");
  _assert_dict_elem(result, "first", "foo");
  _assert_dict_elem(result, "2", "bar");
  _assert_dict_elem(result, "third", "baz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp, regexp_search_forced_list)
{
  FilterXObject *result = filterx_test_list_new();
  _search_with_fillable("foobarbaz", "(?<first>foo)(bar)(?<third>baz)", result);
  _assert_len(result, 4);
  _assert_list_elem(result, 0, "foobarbaz");
  _assert_list_elem(result, 1, "foo");
  _assert_list_elem(result, 2, "bar");
  _assert_list_elem(result, 3, "baz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp, regexp_search_forced_dict)
{
  FilterXObject *result = filterx_test_dict_new();
  _search_with_fillable("foobarbaz", "(foo)(bar)(baz)", result);
  _assert_len(result, 4);
  _assert_dict_elem(result, "0", "foobarbaz");
  _assert_dict_elem(result, "1", "foo");
  _assert_dict_elem(result, "2", "bar");
  _assert_dict_elem(result, "3", "baz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp, regexp_search_unnamed_no_match)
{
  FilterXObject *result = _search("foobarbaz", "(almafa)");
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(list)));
  _assert_len(result, 0);
  filterx_object_unref(result);
}

Test(filterx_expr_regexp, regexp_search_named_no_match)
{
  FilterXObject *result = _search("foobarbaz", "(?<first>almafa)");
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(dict)));
  _assert_len(result, 0);
  filterx_object_unref(result);
}

Test(filterx_expr_regexp, regexp_search_init_error)
{
  _assert_search_init_error("foobarbaz", "(");
}

static FilterXExpr *
_build_subst_func(const gchar *pattern, const gchar *repr, const gchar *str, FilterXFuncRegexpSubstOpts opts)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_non_literal_new(filterx_string_new(str, -1))));
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new(pattern, -1))));
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new(repr, -1))));
  if (opts.global)
    args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_REGEXP_SUBST_FLAG_GLOBAL_NAME,
                                                        filterx_literal_new(filterx_boolean_new(TRUE))));
  if (!opts.jit)
    args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_REGEXP_SUBST_FLAG_JIT_NAME,
                                                        filterx_literal_new(filterx_boolean_new(FALSE))));
  if (opts.ignorecase)
    args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_REGEXP_SUBST_FLAG_IGNORECASE_NAME,
                                                        filterx_literal_new(filterx_boolean_new(TRUE))));
  if (opts.newline)
    args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_REGEXP_SUBST_FLAG_NEWLINE_NAME,
                                                        filterx_literal_new(filterx_boolean_new(TRUE))));
  if (opts.utf8)
    args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_REGEXP_SUBST_FLAG_UTF8_NAME,
                                                        filterx_literal_new(filterx_boolean_new(TRUE))));
  if (opts.groups)
    args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_REGEXP_SUBST_FLAG_GROUPS_NAME,
                                                        filterx_literal_new(filterx_boolean_new(TRUE))));

  GError *err = NULL;
  FilterXExpr *func = filterx_function_regexp_subst_new(filterx_function_args_new(args, NULL), &err);
  cr_assert_null(err);
  return func;
}

static FilterXObject *
_sub(const gchar *pattern, const gchar *repr, const gchar *str, FilterXFuncRegexpSubstOpts opts)
{
  FilterXExpr *func = _build_subst_func(pattern, repr, str, opts);

  FilterXObject *res = filterx_expr_eval(func);
  filterx_expr_unref(func);
  return res;
}

// disabling jit compiler since it confuses valgrind in some cases
// in some test cases we test jit against non-jit, those tests will produce invalid reads in valgrind
// further info: https://stackoverflow.com/questions/74777619/valgrind-conditional-jump-error-with-pcre2-jit-when-reading-from-file

Test(filterx_expr_regexp, regexp_subst_single_replace)
{
  FilterXFuncRegexpSubstOpts opts = {};
  FilterXObject *result = _sub("oo", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "fXbarbaz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp, regexp_subst_single_replace_with_global)
{
  FilterXFuncRegexpSubstOpts opts = {.global = TRUE};
  FilterXObject *result = _sub("oo", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "fXbarbaz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp, regexp_subst_multi_replace)
{
  FilterXFuncRegexpSubstOpts opts = {};
  FilterXObject *result = _sub("a", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "foobXrbaz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp, regexp_subst_multi_replace_with_global)
{
  FilterXFuncRegexpSubstOpts opts = {.global = TRUE};
  FilterXObject *result = _sub("a", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "foobXrbXz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp, regexp_subst_zero_length_matches)
{
  FilterXFuncRegexpSubstOpts opts = {};
  FilterXObject *result = _sub("u*", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "XfoobarbazX");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp, regexp_subst_zero_length_matches_with_global)
{
  FilterXFuncRegexpSubstOpts opts = {.global = TRUE};
  FilterXObject *result = _sub("u*", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "XfXoXoXbXaXrXbXaXzX");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp, regexp_subst_zero_length_matches_with_char_matches)
{
  FilterXFuncRegexpSubstOpts opts = {};
  FilterXObject *result = _sub("a*", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "XfoobarbazX");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp, regexp_subst_zero_length_matches_with_char_matches_with_global)
{
  FilterXFuncRegexpSubstOpts opts = {.global = TRUE, .jit=FALSE};
  FilterXObject *result = _sub("a*", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "XfXoXoXbXXrXbXXzX");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp, regexp_subst_at_beginning)
{
  FilterXFuncRegexpSubstOpts opts = {};
  FilterXObject *result = _sub("fo", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "Xobarbaz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp, regexp_subst_at_beginning_with_global)
{
  FilterXFuncRegexpSubstOpts opts = {.global = TRUE};
  FilterXObject *result = _sub("fo", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "Xobarbaz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp, regexp_subst_at_the_end)
{
  FilterXFuncRegexpSubstOpts opts = {};
  FilterXObject *result = _sub("az", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "foobarbX");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp, regexp_subst_at_the_end_with_global)
{
  FilterXFuncRegexpSubstOpts opts = {.global = TRUE};
  FilterXObject *result = _sub("az", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "foobarbX");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp, regexp_subst_multi_replace_multi_pattern)
{
  FilterXFuncRegexpSubstOpts opts = {};
  FilterXObject *result = _sub("(a|o)", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "fXobarbaz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp, regexp_subst_multi_replace_multi_pattern_with_global)
{
  FilterXFuncRegexpSubstOpts opts = {.global = TRUE};
  FilterXObject *result = _sub("(a|o)", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "fXXbXrbXz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp, regexp_subst_accept_end_literal)
{
  FilterXFuncRegexpSubstOpts opts = {};
  FilterXObject *result = _sub("ba.$", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "foobarX");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp, regexp_subst_accept_end_literal_with_global)
{
  FilterXFuncRegexpSubstOpts opts = {.global = TRUE};
  FilterXObject *result = _sub("ba.$", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "foobarX");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp, regexp_subst_accept_groups)
{
  FilterXFuncRegexpSubstOpts opts = {};
  FilterXObject *result = _sub("(o)*(ba)", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "fXrbaz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp, regexp_subst_accept_groups_with_global)
{
  FilterXFuncRegexpSubstOpts opts = {.global = TRUE};
  FilterXObject *result = _sub("(o)*(ba)", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "fXrXz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp, regexp_subst_nojit_arg)
{
  FilterXFuncRegexpSubstOpts opts = {.jit = TRUE};
  FilterXExpr *func = _build_subst_func("o", "X", "foobarbaz", opts);
  cr_assert_not_null(func);
  cr_assert(filterx_regexp_subst_is_jit_enabled(func));
  filterx_expr_unref(func);

  FilterXFuncRegexpSubstOpts opts_nojit = {};
  FilterXExpr *func_nojit = _build_subst_func("o", "X", "foobarbaz", opts_nojit);
  cr_assert_not_null(func_nojit);
  cr_assert(!filterx_regexp_subst_is_jit_enabled(func_nojit));
  filterx_expr_unref(func_nojit);
}

Test(filterx_expr_regexp, regexp_subst_match_opt_ignorecase)
{
  FilterXFuncRegexpSubstOpts opts = {.global = TRUE};
  FilterXObject *result = _sub("(O|A)", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "foobarbaz");
  filterx_object_unref(result);

  FilterXFuncRegexpSubstOpts opts_alt = {.ignorecase = TRUE, .global = TRUE};
  FilterXObject *result_alt = _sub("(O|A)", "X", "foobarbaz", opts_alt);
  cr_assert(filterx_object_is_type(result_alt, &FILTERX_TYPE_NAME(string)));
  const gchar *res_alt = filterx_string_get_value_ref(result_alt, NULL);
  cr_assert_str_eq(res_alt, "fXXbXrbXz");
  filterx_object_unref(result_alt);
}

Test(filterx_expr_regexp, regexp_subst_match_opt_ignorecase_nojit)
{
  // check whether the CASELESS option applied with non-jit pattern
  FilterXFuncRegexpSubstOpts opts = {.global=TRUE};
  FilterXObject *result = _sub("(O|A)", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "foobarbaz");
  filterx_object_unref(result);

  FilterXFuncRegexpSubstOpts opts_alt = {.ignorecase = TRUE, .global = TRUE, .jit = TRUE};
  FilterXObject *result_alt = _sub("(O|A)", "X", "foobarbaz", opts_alt);
  cr_assert(filterx_object_is_type(result_alt, &FILTERX_TYPE_NAME(string)));
  const gchar *res_alt = filterx_string_get_value_ref(result_alt, NULL);
  cr_assert_str_eq(res_alt, "fXXbXrbXz");
  filterx_object_unref(result_alt);
}

Test(filterx_expr_regexp, regexp_subst_group_subst)
{
  FilterXFuncRegexpSubstOpts opts = {};
  FilterXObject *result = _sub("(\\d{2})-(\\d{2})-(\\d{4})", "\\3-\\2-\\1", "25-02-2022", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "\\3-\\2-\\1");
  filterx_object_unref(result);

  FilterXFuncRegexpSubstOpts opts_alt = {.groups = TRUE};
  FilterXObject *result_alt = _sub("(\\d{2})-(\\d{2})-(\\d{4})", "\\3-\\2-\\1", "25-02-2022", opts_alt);
  cr_assert(filterx_object_is_type(result_alt, &FILTERX_TYPE_NAME(string)));
  const gchar *res_alt = filterx_string_get_value_ref(result_alt, NULL);
  cr_assert_str_eq(res_alt, "2022-02-25");
  filterx_object_unref(result_alt);
}

Test(filterx_expr_regexp, regexp_subst_group_subst_without_ref)
{
  FilterXFuncRegexpSubstOpts opts = {.groups = TRUE};
  FilterXObject *result = _sub("(\\d{2})-(\\d{2})-(\\d{4})", "group without ref", "25-02-2022", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "group without ref");
  filterx_object_unref(result);
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

TestSuite(filterx_expr_regexp, .init = setup, .fini = teardown);
