/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 shifter <shifter@axoflow.com>
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
#include "libtest/cr_template.h"
#include "libtest/filterx-lib.h"

#include "filterx/filterx-object.h"
#include "filterx/object-primitive.h"
#include "filterx/expr-comparison.h"
#include "filterx/filterx-expr.h"
#include "filterx/expr-literal.h"
#include "filterx/object-string.h"
#include "filterx/object-null.h"
#include "filterx/object-datetime.h"
#include "filterx/object-message-value.h"
#include "filterx/expr-null-coalesce.h"
#include "filterx/func-istype.h"
#include "filterx/filterx-eval.h"
#include "filterx/func-len.h"
#include "filterx/expr-function.h"
#include "filterx/filterx-object-istype.h"

#include "apphook.h"
#include "scratch-buffers.h"

Test(expr_null_coalesce, test_coalescing_soft_null)
{
  FilterXExpr *coalesce = filterx_null_coalesce_new(filterx_literal_new(filterx_null_new()),
                                                    filterx_non_literal_new(filterx_test_unknown_object_new()));
  cr_assert(coalesce);
  FilterXObject *res = filterx_expr_eval(coalesce);
  cr_assert(res);
  cr_assert(filterx_object_is_type(res, &FILTERX_TYPE_NAME(test_unknown_object)));
  filterx_expr_unref(coalesce);
  filterx_object_unref(res);
}

Test(expr_null_coalesce, test_coalescing_supressing_lhs_eval_error)
{
  FilterXExpr *err_expr = filterx_dummy_error_new("lhs error");
  cr_assert_not_null(err_expr);

  // passing errorous expression as lhs
  FilterXExpr *coalesce = filterx_null_coalesce_new(err_expr, filterx_non_literal_new(filterx_test_unknown_object_new()));
  cr_assert(coalesce);

  // eval returns rhs value, since lhs fails during eval
  FilterXObject *res = filterx_expr_eval(coalesce);
  cr_assert(res);
  cr_assert(filterx_object_is_type(res, &FILTERX_TYPE_NAME(test_unknown_object)));

  // lhs expr eval errors must supressed by null_coalesce
  const gchar *last_error = filterx_eval_get_last_error();
  cr_assert_null(last_error);

  filterx_expr_unref(coalesce);
  filterx_object_unref(res);
}

Test(expr_null_coalesce, test_coalescing_keep_rhs_eval_error)
{
  const gchar *error_msg = "rhs error";
  FilterXExpr *err_expr = filterx_dummy_error_new(error_msg);

  // passing errorous expression as rhs
  FilterXExpr *coalesce = filterx_null_coalesce_new(filterx_non_literal_new(filterx_null_new()),
                                                    err_expr);
  cr_assert(coalesce);

  // null_coalesce returns null, since lhs is null and rhs fails
  FilterXObject *res = filterx_expr_eval(coalesce);
  cr_assert_null(res);

  // rhs expr eval errors must remain intact
  const gchar *last_error = filterx_eval_get_last_error();
  cr_assert_not_null(last_error);
  cr_assert_str_eq(error_msg, last_error);

  filterx_expr_unref(coalesce);
  filterx_object_unref(res);
}

Test(expr_null_coalesce, test_coalescing_keep_rhs_eval_error_on_double_fail)
{
  FilterXExpr *err_expr_lhs = filterx_dummy_error_new("lhs error");

  const gchar *error_msg = "rhs error";
  FilterXExpr *err_expr_rhs = filterx_dummy_error_new(error_msg);

  // passing errorous expressions
  FilterXExpr *coalesce = filterx_null_coalesce_new(err_expr_lhs, err_expr_rhs);
  cr_assert(coalesce);

  // null_coalesce returns null, since both lhs and rhs fails
  FilterXObject *res = filterx_expr_eval(coalesce);
  cr_assert_null(res);

  // rhs expr eval errors must remain intact
  const gchar *last_error = filterx_eval_get_last_error();
  cr_assert_not_null(last_error);
  cr_assert_str_eq(error_msg, last_error);

  filterx_expr_unref(coalesce);
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

TestSuite(expr_null_coalesce, .init = setup, .fini = teardown);
