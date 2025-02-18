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

#include "filterx/expr-regexp.h"
#include "filterx/expr-regexp-subst.h"
#include "filterx/expr-literal.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "filterx/object-dict-interface.h"
#include "filterx/object-list-interface.h"
#include "filterx/filterx-object-istype.h"
#include "apphook.h"
#include "scratch-buffers.h"
#include "compat/pcre.h"

typedef struct FilterXFuncRegexpSubstOpts_
{
  gboolean global;
  gboolean jit;
  gboolean utf8;
  gboolean ignorecase;
  gboolean newline;
  gboolean groups;
} FilterXFuncRegexpSubstOpts;

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
  if (!opts.groups)
    args = g_list_append(args, filterx_function_arg_new(FILTERX_FUNC_REGEXP_SUBST_FLAG_GROUPS_NAME,
                                                        filterx_literal_new(filterx_boolean_new(FALSE))));

  GError *err = NULL;
  FilterXExpr *func = filterx_function_regexp_subst_new(filterx_function_args_new(args, NULL), &err);
  cr_assert_null(err);

  func = filterx_expr_optimize(func);
  cr_assert(filterx_expr_init(func, configuration));

  return func;
}

static FilterXObject *
_sub(const gchar *pattern, const gchar *repr, const gchar *str, FilterXFuncRegexpSubstOpts opts)
{
  FilterXExpr *func = _build_subst_func(pattern, repr, str, opts);

  FilterXObject *res = filterx_expr_eval(func);

  filterx_expr_deinit(func, configuration);
  filterx_expr_unref(func);
  return res;
}

// disabling jit compiler since it confuses valgrind in some cases
// in some test cases we test jit against non-jit, those tests will produce invalid reads in valgrind
// further info: https://stackoverflow.com/questions/74777619/valgrind-conditional-jump-error-with-pcre2-jit-when-reading-from-file

Test(filterx_expr_regexp_subst, regexp_subst_single_replace)
{
  FilterXFuncRegexpSubstOpts opts = {};
  FilterXObject *result = _sub("oo", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "fXbarbaz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_subst, regexp_subst_single_replace_with_global)
{
  FilterXFuncRegexpSubstOpts opts = {.global = TRUE};
  FilterXObject *result = _sub("oo", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "fXbarbaz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_subst, regexp_subst_multi_replace)
{
  FilterXFuncRegexpSubstOpts opts = {};
  FilterXObject *result = _sub("a", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "foobXrbaz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_subst, regexp_subst_multi_replace_with_global)
{
  FilterXFuncRegexpSubstOpts opts = {.global = TRUE};
  FilterXObject *result = _sub("a", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "foobXrbXz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_subst, regexp_subst_zero_length_matches)
{
  FilterXFuncRegexpSubstOpts opts = {};
  FilterXObject *result = _sub("u*", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "XfoobarbazX");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_subst, regexp_subst_zero_length_matches_with_global)
{
  FilterXFuncRegexpSubstOpts opts = {.global = TRUE};
  FilterXObject *result = _sub("u*", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "XfXoXoXbXaXrXbXaXzX");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_subst, regexp_subst_zero_length_matches_with_char_matches)
{
  FilterXFuncRegexpSubstOpts opts = {};
  FilterXObject *result = _sub("a*", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "XfoobarbazX");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_subst, regexp_subst_zero_length_matches_with_char_matches_with_global)
{
  FilterXFuncRegexpSubstOpts opts = {.global = TRUE, .jit=FALSE};
  FilterXObject *result = _sub("a*", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "XfXoXoXbXXrXbXXzX");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_subst, regexp_subst_at_beginning)
{
  FilterXFuncRegexpSubstOpts opts = {};
  FilterXObject *result = _sub("fo", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "Xobarbaz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_subst, regexp_subst_at_beginning_with_global)
{
  FilterXFuncRegexpSubstOpts opts = {.global = TRUE};
  FilterXObject *result = _sub("fo", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "Xobarbaz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_subst, regexp_subst_at_the_end)
{
  FilterXFuncRegexpSubstOpts opts = {};
  FilterXObject *result = _sub("az", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "foobarbX");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_subst, regexp_subst_at_the_end_with_global)
{
  FilterXFuncRegexpSubstOpts opts = {.global = TRUE};
  FilterXObject *result = _sub("az", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "foobarbX");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_subst, regexp_subst_multi_replace_multi_pattern)
{
  FilterXFuncRegexpSubstOpts opts = {};
  FilterXObject *result = _sub("(a|o)", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "fXobarbaz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_subst, regexp_subst_multi_replace_multi_pattern_with_global)
{
  FilterXFuncRegexpSubstOpts opts = {.global = TRUE};
  FilterXObject *result = _sub("(a|o)", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "fXXbXrbXz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_subst, regexp_subst_accept_end_literal)
{
  FilterXFuncRegexpSubstOpts opts = {};
  FilterXObject *result = _sub("ba.$", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "foobarX");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_subst, regexp_subst_accept_end_literal_with_global)
{
  FilterXFuncRegexpSubstOpts opts = {.global = TRUE};
  FilterXObject *result = _sub("ba.$", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "foobarX");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_subst, regexp_subst_accept_groups)
{
  FilterXFuncRegexpSubstOpts opts = {};
  FilterXObject *result = _sub("(o)*(ba)", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "fXrbaz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_subst, regexp_subst_accept_groups_with_global)
{
  FilterXFuncRegexpSubstOpts opts = {.global = TRUE};
  FilterXObject *result = _sub("(o)*(ba)", "X", "foobarbaz", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "fXrXz");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_subst, regexp_subst_nojit_arg)
{
  FilterXFuncRegexpSubstOpts opts = {.jit = TRUE};
  FilterXExpr *func = _build_subst_func("o", "X", "foobarbaz", opts);
  cr_assert_not_null(func);
  cr_assert(filterx_regexp_subst_is_jit_enabled(func));
  filterx_expr_deinit(func, configuration);
  filterx_expr_unref(func);

  FilterXFuncRegexpSubstOpts opts_nojit = {};
  FilterXExpr *func_nojit = _build_subst_func("o", "X", "foobarbaz", opts_nojit);
  cr_assert_not_null(func_nojit);
  cr_assert(!filterx_regexp_subst_is_jit_enabled(func_nojit));
  filterx_expr_deinit(func_nojit, configuration);
  filterx_expr_unref(func_nojit);
}

Test(filterx_expr_regexp_subst, regexp_subst_match_opt_ignorecase)
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

Test(filterx_expr_regexp_subst, regexp_subst_match_opt_ignorecase_nojit)
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

Test(filterx_expr_regexp_subst, regexp_subst_group_subst)
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

Test(filterx_expr_regexp_subst, regexp_subst_group_subst_without_ref)
{
  FilterXFuncRegexpSubstOpts opts = {.groups = TRUE};
  FilterXObject *result = _sub("(\\d{2})-(\\d{2})-(\\d{4})", "group without ref", "25-02-2022", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "group without ref");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_subst, regexp_subst_group_reference_with_multiple_digits)
{
  FilterXFuncRegexpSubstOpts opts = {.groups = TRUE};
  FilterXObject *result =
    _sub("(\\d{2})(\\d{2})(\\d{2})(\\d{2})(\\d{2})(\\d{2})(\\d{2})(\\d{2})(\\d{2})(\\d{2})(\\d{2})(\\d{2})",
         "\\12-\\11-\\10-\\9\\8\\7\\6\\5\\4\\3\\2\\1", "010203040506070809101112", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "12-11-10-090807060504030201");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_subst, regexp_subst_group_do_not_replace_unknown_ref)
{
  FilterXFuncRegexpSubstOpts opts = {.groups = TRUE};
  FilterXObject *result = _sub("(\\d{2})(\\d{2})(\\d{2})",
                               "\\3\\20\\1", "010203", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "03\\2001");
  filterx_object_unref(result);
}

Test(filterx_expr_regexp_subst, regexp_subst_group_limited_digits_and_zero_prefixes)
{
  FilterXFuncRegexpSubstOpts opts = {.groups = TRUE};
  FilterXObject *result = _sub("(\\w+),(\\w+),(\\w+)", "\\3\\02\\0013.14", "baz,bar,foo", opts);
  cr_assert(filterx_object_is_type(result, &FILTERX_TYPE_NAME(string)));
  const gchar *res = filterx_string_get_value_ref(result, NULL);
  cr_assert_str_eq(res, "foobarbaz3.14");
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

TestSuite(filterx_expr_regexp_subst, .init = setup, .fini = teardown);
