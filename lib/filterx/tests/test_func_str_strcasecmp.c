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
#include "apphook.h"
#include "scratch-buffers.h"

static FilterXExpr *
_create_optimized_strcasecmp_expr(const gchar *a, const gchar *b)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new(a, -1))));
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_literal_new(filterx_string_new(b, -1))));

  GError *error = NULL;
  FilterXExpr *fn = filterx_function_strcasecmp_new(filterx_function_args_new(args, NULL), &error);
  cr_assert_null(error);

  return filterx_expr_optimize(fn);
}

static void
_assert_strcasecmp_folded(const gchar *a, const gchar *b, const gchar *expected)
{
  FilterXExpr *fn = _create_optimized_strcasecmp_expr(a, b);
  cr_assert(filterx_expr_is_literal(fn));

  FilterXObject *res = init_and_eval_expr(fn);
  cr_assert_not_null(res);
  assert_object_repr_equals(res, expected);

  filterx_object_unref(res);
  filterx_expr_unref(fn);
}

Test(filterx_func_strcasecmp, two_literals_are_folded)
{
  _assert_strcasecmp_folded("Foo", "foo", "0");
  _assert_strcasecmp_folded("abc", "abd", "-1");
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

TestSuite(filterx_func_strcasecmp, .init = setup, .fini = teardown);
