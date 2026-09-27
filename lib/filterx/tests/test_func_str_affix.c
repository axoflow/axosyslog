/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Attila Szakacs <attila.szakacs@axoflow.com>
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

#include "filterx/func-str.h"
#include "filterx/object-string.h"
#include "filterx/expr-function.h"
#include "filterx/expr-literal.h"
#include "filterx/expr-literal-container.h"

#include "apphook.h"
#include "scratch-buffers.h"

typedef FilterXExpr *(*FilterXAffixCtor)(FilterXFunctionArgs *args, GError **error);

static FilterXExpr *
_create_optimized_affix_expr(FilterXAffixCtor ctor, const gchar *haystack, FilterXExpr *needle)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new(haystack, -1))));
  args = g_list_append(args, filterx_function_arg_new(NULL, needle));

  GError *error = NULL;
  FilterXExpr *fn = ctor(filterx_function_args_new(args, NULL), &error);
  cr_assert_null(error);

  return filterx_expr_optimize(fn);
}

static void
_assert_affix_not_folded(FilterXAffixCtor ctor, const gchar *haystack, FilterXExpr *needle, const gchar *expected)
{
  FilterXExpr *fn = _create_optimized_affix_expr(ctor, haystack, needle);
  cr_assert_not(filterx_expr_is_literal(fn), "a non-literal needle must not be folded at optimize time");

  FilterXObject *res = init_and_eval_expr(fn);
  cr_assert_not_null(res);
  assert_object_repr_equals(res, expected);

  filterx_object_unref(res);
  filterx_expr_unref(fn);
}

static FilterXExpr *
_non_literal_needle(const gchar *needle)
{
  return filterx_object_expr_new(filterx_string_new(needle, -1));
}

Test(filterx_func_affix, includes_with_non_literal_needle)
{
  _assert_affix_not_folded(filterx_function_includes_new, ",abort,deny,", _non_literal_needle(",deny,"), "true");
  _assert_affix_not_folded(filterx_function_includes_new, ",abort,deny,", _non_literal_needle(",allow,"), "false");
}

Test(filterx_func_affix, startswith_with_non_literal_needle)
{
  _assert_affix_not_folded(filterx_function_startswith_new, "foobar", _non_literal_needle("foo"), "true");
  _assert_affix_not_folded(filterx_function_startswith_new, "foobar", _non_literal_needle("bar"), "false");
}

Test(filterx_func_affix, endswith_with_non_literal_needle)
{
  _assert_affix_not_folded(filterx_function_endswith_new, "foobar", _non_literal_needle("bar"), "true");
  _assert_affix_not_folded(filterx_function_endswith_new, "foobar", _non_literal_needle("foo"), "false");
}

Test(filterx_func_affix, includes_with_non_literal_needle_in_literal_list)
{
  _assert_affix_not_folded(filterx_function_includes_new, ",abort,deny,",
                           filterx_literal_list_of(filterx_literal_new(filterx_string_new(",allow,", -1)),
                                                   _non_literal_needle(",deny,"), NULL), "true");
}

Test(filterx_func_affix, literal_haystack_and_needle_is_folded)
{
  FilterXExpr *fn = _create_optimized_affix_expr(filterx_function_includes_new, ",abort,deny,",
                                                 filterx_literal_new(filterx_string_new(",deny,", -1)));
  cr_assert(filterx_expr_is_literal(fn));

  FilterXObject *res = init_and_eval_expr(fn);
  cr_assert_not_null(res);
  assert_object_repr_equals(res, "true");

  filterx_object_unref(res);
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

TestSuite(filterx_func_affix, .init = setup, .fini = teardown);
