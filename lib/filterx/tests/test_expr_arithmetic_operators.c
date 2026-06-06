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

Test(filterx_expr_arithmetic, division_by_zero_does_not_raise_sigfpe)
{
  FilterXObject *res = _eval_binop(filterx_operator_division_new, 1, 0);
  cr_assert_null(res);
}

Test(filterx_expr_arithmetic, modulo_by_zero_does_not_raise_sigfpe)
{
  FilterXObject *res = _eval_binop(filterx_operator_modulo_new, 1, 0);
  cr_assert_null(res);
}

Test(filterx_expr_arithmetic, int64_min_divided_by_minus_one_does_not_overflow)
{
  FilterXObject *res = _eval_binop(filterx_operator_division_new, G_MININT64, -1);
  cr_assert_null(res);
}

Test(filterx_expr_arithmetic, int64_min_modulo_minus_one_does_not_overflow)
{
  FilterXObject *res = _eval_binop(filterx_operator_modulo_new, G_MININT64, -1);
  cr_assert_null(res);
}

Test(filterx_expr_arithmetic, normal_division_still_works)
{
  FilterXObject *res = _eval_binop(filterx_operator_division_new, 6, 2);
  cr_assert_not_null(res);
  gint64 value;
  cr_assert(filterx_integer_unwrap(res, &value));
  cr_assert_eq(value, 3);
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
