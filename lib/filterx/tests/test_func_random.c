/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Mate Ory <mate.ory@axoflow.com>
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
#include <math.h>
#include <stdlib.h>
#include "libtest/filterx-lib.h"

#include "filterx/func-random.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "filterx/expr-function.h"
#include "filterx/expr-literal.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-private.h"

#include "apphook.h"
#include "scratch-buffers.h"

static FilterXExpr *
_create_random_expr(FilterXObject *bound)
{
  GList *args = bound ? g_list_append(NULL, filterx_function_arg_new(NULL, filterx_object_expr_new(bound))) : NULL;
  GError *error = NULL;
  FilterXExpr *fn = filterx_simple_function_new("random", filterx_function_args_new(args, NULL),
                                                filterx_simple_function_random, &error);
  cr_assert_null(error);
  return fn;
}

Test(filterx_func_random, no_args_returns_double_in_unit_interval)
{
  FilterXExpr *fn = _create_random_expr(NULL);
  gdouble sum = 0;

  for (gint i = 0; i < 10000; i++)
    {
      FilterXObject *res = init_and_eval_expr(fn);
      gdouble v;
      cr_assert(res && filterx_double_unwrap(res, &v));
      filterx_object_unref(res);
      cr_assert(v >= 0.0 && v < 1.0);
      sum += v;
    }

  /* mean of U[0,1) over 10000 draws: sigma ~0.0029, so this is a >10 sigma bound */
  cr_assert_float_eq(sum / 10000, 0.5, 0.03);
  filterx_expr_unref(fn);
}

Test(filterx_func_random, bound_returns_uniform_integer_in_range)
{
  FilterXExpr *fn = _create_random_expr(filterx_integer_new(4));
  gint hits[4] = { 0 };

  for (gint i = 0; i < 4000; i++)
    {
      FilterXObject *res = init_and_eval_expr(fn);
      gint64 v;
      cr_assert(res && filterx_integer_unwrap(res, &v));
      filterx_object_unref(res);
      cr_assert(v >= 0 && v < 4);
      hits[v]++;
    }

  for (gint i = 0; i < 4; i++)
    cr_assert_gt(hits[i], 800, "value %d drawn only %d times out of 4000", i, hits[i]);
  filterx_expr_unref(fn);
}

static void
_assert_rejected(FilterXObject *bound)
{
  FilterXExpr *fn = _create_random_expr(bound);
  cr_assert_null(init_and_eval_expr(fn));
  filterx_expr_unref(fn);
}

Test(filterx_func_random, rejects_invalid_bound)
{
  _assert_rejected(filterx_integer_new(0));
  _assert_rejected(filterx_double_new(2.5));
  _assert_rejected(filterx_string_new("10", -1));
}

static FilterXExpr *
_create_random_bool_expr(FilterXExpr *probability, GError **error)
{
  GList *args = probability ? g_list_append(NULL, filterx_function_arg_new(NULL, probability)) : NULL;
  return filterx_function_random_bool_new(filterx_function_args_new(args, NULL), error);
}

static gint
_count_true(FilterXExpr *probability, gint n)
{
  GError *error = NULL;
  FilterXExpr *fn = _create_random_bool_expr(probability, &error);
  cr_assert_null(error);
  gint count = 0;
  for (gint i = 0; i < n; i++)
    {
      FilterXObject *res = init_and_eval_expr(fn);
      gboolean value;
      cr_assert(res && filterx_boolean_unwrap(res, &value));
      filterx_object_unref(res);
      count += value;
    }

  filterx_expr_unref(fn);
  return count;
}

/* literals are precomputed by the constructor, other expressions are evaluated per call */
Test(filterx_func_random, bool_probability_is_respected)
{
  cr_assert_eq(_count_true(filterx_literal_new(filterx_integer_new(0)), 1000), 0);
  cr_assert_eq(_count_true(filterx_object_expr_new(filterx_double_new(1.0)), 1000), 1000);
  /* binomial(10000, p <= 0.5): sigma <= 50, so these are >10 sigma bounds */
  cr_assert_lt(abs(_count_true(filterx_literal_new(filterx_double_new(0.25)), 10000) - 2500), 500);
  cr_assert_lt(abs(_count_true(filterx_object_expr_new(filterx_double_new(0.25)), 10000) - 2500), 500);
  cr_assert_lt(abs(_count_true(NULL, 10000) - 5000), 500);
}

Test(filterx_func_random, bool_rejects_invalid_probability)
{
  GError *error = NULL;
  cr_assert_null(_create_random_bool_expr(filterx_literal_new(filterx_double_new(1.1)), &error));
  cr_assert_not_null(error);
  g_clear_error(&error);
  FilterXExpr *fn = _create_random_bool_expr(filterx_object_expr_new(filterx_double_new(NAN)), &error);
  cr_assert_null(error);
  cr_assert_null(init_and_eval_expr(fn));
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

TestSuite(filterx_func_random, .init = setup, .fini = teardown);
