/*
 * Copyright (c) 2026 Axoflow
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

#include "filterx/expr-arithmetic-operators.h"
#include "filterx/object-primitive.h"
#include "filterx/filterx-object.h"

#include "apphook.h"
#include "scratch-buffers.h"

static FilterXObject *
_eval_binop(FilterXExpr *(*ctor)(FilterXExpr *, FilterXExpr *), gint64 lhs, gint64 rhs)
{
  FilterXExpr *op = ctor(filterx_object_expr_new(filterx_integer_new(lhs)),
                         filterx_object_expr_new(filterx_integer_new(rhs)));
  FilterXObject *res = init_and_eval_expr(op);
  filterx_expr_unref(op);
  return res;
}

static FilterXObject *
_eval_binop_double(FilterXExpr *(*ctor)(FilterXExpr *, FilterXExpr *), gdouble lhs, gdouble rhs)
{
  FilterXExpr *op = ctor(filterx_object_expr_new(filterx_double_new(lhs)),
                         filterx_object_expr_new(filterx_double_new(rhs)));
  FilterXObject *res = init_and_eval_expr(op);
  filterx_expr_unref(op);
  return res;
}

Test(filterx_expr_arithmetic, division_by_zero_does_not_raise_sigfpe)
{
  FilterXObject *res = _eval_binop(filterx_arithmetic_operator_division_new, 1, 0);
  cr_assert_null(res);
}

Test(filterx_expr_arithmetic, modulo_by_zero_does_not_raise_sigfpe)
{
  FilterXObject *res = _eval_binop(filterx_arithmetic_operator_modulo_new, 1, 0);
  cr_assert_null(res);
}

Test(filterx_expr_arithmetic, int64_min_divided_by_minus_one_does_not_overflow)
{
  FilterXObject *res = _eval_binop(filterx_arithmetic_operator_division_new, G_MININT64, -1);
  cr_assert_null(res);
}

Test(filterx_expr_arithmetic, int64_min_modulo_minus_one_does_not_overflow)
{
  FilterXObject *res = _eval_binop(filterx_arithmetic_operator_modulo_new, G_MININT64, -1);
  cr_assert_null(res);
}

Test(filterx_expr_arithmetic, normal_division_still_works)
{
  FilterXObject *res = _eval_binop(filterx_arithmetic_operator_division_new, 6, 2);
  cr_assert_not_null(res);
  gint64 value;
  cr_assert(filterx_integer_unwrap(res, &value));
  cr_assert_eq(value, 3);
  filterx_object_unref(res);
}

Test(filterx_expr_arithmetic, int64_subtraction_overflow_is_rejected)
{
  FilterXObject *res = _eval_binop(filterx_arithmetic_operator_substraction_new, G_MAXINT64, -1);
  cr_assert_null(res);
}

Test(filterx_expr_arithmetic, int64_multiplication_overflow_is_rejected)
{
  FilterXObject *res = _eval_binop(filterx_arithmetic_operator_multiplication_new, G_MAXINT64, 2);
  cr_assert_null(res);
}

Test(filterx_expr_arithmetic, normal_subtraction_still_works)
{
  FilterXObject *res = _eval_binop(filterx_arithmetic_operator_substraction_new, 10, 3);
  cr_assert_not_null(res);
  gint64 value;
  cr_assert(filterx_integer_unwrap(res, &value));
  cr_assert_eq(value, 7);
  filterx_object_unref(res);
}

Test(filterx_expr_arithmetic, normal_multiplication_still_works)
{
  FilterXObject *res = _eval_binop(filterx_arithmetic_operator_multiplication_new, 6, 7);
  cr_assert_not_null(res);
  gint64 value;
  cr_assert(filterx_integer_unwrap(res, &value));
  cr_assert_eq(value, 42);
  filterx_object_unref(res);
}

Test(filterx_expr_arithmetic, uminus_of_int64_min_is_rejected)
{
  FilterXExpr *op = filterx_arithmetic_operator_uminus_new(filterx_object_expr_new(filterx_integer_new(G_MININT64)));
  FilterXObject *res = init_and_eval_expr(op);
  filterx_expr_unref(op);
  cr_assert_null(res);
}

Test(filterx_expr_arithmetic, normal_uminus_still_works)
{
  FilterXExpr *op = filterx_arithmetic_operator_uminus_new(filterx_object_expr_new(filterx_integer_new(5)));
  FilterXObject *res = init_and_eval_expr(op);
  filterx_expr_unref(op);
  cr_assert_not_null(res);
  gint64 value;
  cr_assert(filterx_integer_unwrap(res, &value));
  cr_assert_eq(value, -5);
  filterx_object_unref(res);
}

Test(filterx_expr_arithmetic, double_division_by_zero_is_rejected)
{
  cr_assert_null(_eval_binop_double(filterx_arithmetic_operator_division_new, 1.0, 0.0));
}

Test(filterx_expr_arithmetic, double_zero_over_zero_is_rejected)
{
  cr_assert_null(_eval_binop_double(filterx_arithmetic_operator_division_new, 0.0, 0.0));
}

Test(filterx_expr_arithmetic, double_multiplication_overflow_is_rejected)
{
  cr_assert_null(_eval_binop_double(filterx_arithmetic_operator_multiplication_new, 1e308, 1e308));
}

Test(filterx_expr_arithmetic, double_subtraction_overflow_is_rejected)
{
  cr_assert_null(_eval_binop_double(filterx_arithmetic_operator_substraction_new, -1e308, 1e308));
}

Test(filterx_expr_arithmetic, normal_double_division_still_works)
{
  FilterXObject *res = _eval_binop_double(filterx_arithmetic_operator_division_new, 6.0, 2.0);
  cr_assert_not_null(res);
  GenericNumber gn = filterx_primitive_get_value(res);
  cr_assert_float_eq(gn_as_double(&gn), 3.0, 0.00001);
  filterx_object_unref(res);
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

TestSuite(filterx_expr_arithmetic, .init = setup, .fini = teardown);
