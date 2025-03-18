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
#include "filterx/expr-plus.h"
#include "filterx/json-repr.h"
#include "filterx/object-list-interface.h"
#include "filterx/object-dict-interface.h"

#include "apphook.h"
#include "scratch-buffers.h"

Test(expr_plus, test_string_success)
{
  FilterXExpr *lhs = filterx_literal_new(filterx_string_new("foo", -1));
  FilterXExpr *rhs = filterx_literal_new(filterx_string_new("bar", -1));


  FilterXExpr *expr = filterx_operator_plus_new(lhs, rhs);
  cr_assert_not_null(expr);

  FilterXObject *obj = filterx_expr_eval(expr);
  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(string)));


  gsize size;
  const gchar *res = filterx_string_get_value_ref(obj, &size);

  cr_assert_str_eq(res, "foobar");

  filterx_object_unref(obj);
  filterx_expr_unref(expr);
}

Test(expr_plus, test_string_add_wrong_type)
{
  FilterXExpr *lhs = filterx_literal_new(filterx_string_new("foo", -1));
  FilterXExpr *rhs = filterx_literal_new(filterx_null_new());

  FilterXExpr *expr = filterx_operator_plus_new(lhs, rhs);
  cr_assert_not_null(expr);

  FilterXObject *obj = filterx_expr_eval(expr);
  cr_assert_null(obj);
  filterx_expr_unref(expr);
}

#define TEST_EPOCH 1577836800000000 // 2020-01-01T00:00:00 in usec

Test(expr_plus, test_datetime_add_datetime)
{
  UnixTime lhs_time = unix_time_from_unix_epoch_usec(TEST_EPOCH);
  UnixTime rhs_time = unix_time_from_unix_epoch_usec(3600000000); // 1 h

  FilterXExpr *lhs = filterx_literal_new(filterx_datetime_new(&lhs_time));
  FilterXExpr *rhs = filterx_literal_new(filterx_datetime_new(&rhs_time));

  FilterXExpr *expr = filterx_operator_plus_new(lhs, rhs);
  cr_assert_not_null(expr);

  FilterXObject *obj = filterx_expr_eval(expr);
  cr_assert_null(obj); // datetime + datetime operation is not supported currently

  filterx_object_unref(obj);
  filterx_expr_unref(expr);
}

Test(expr_plus, test_datetime_add_integer)
{
  UnixTime lhs_time = unix_time_from_unix_epoch_usec(TEST_EPOCH);

  FilterXExpr *lhs = filterx_literal_new(filterx_datetime_new(&lhs_time));
  FilterXExpr *rhs = filterx_literal_new(filterx_integer_new(3600000000)); // 1h in usec

  FilterXExpr *expr = filterx_operator_plus_new(lhs, rhs);
  cr_assert_not_null(expr);

  FilterXObject *obj = filterx_expr_eval(expr);
  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(datetime)));

  assert_object_repr_equals(obj, "2020-01-01T01:00:00.000+00:00");

  filterx_object_unref(obj);
  filterx_expr_unref(expr);
}

Test(expr_plus, test_datetime_add_double)
{
  UnixTime lhs_time = unix_time_from_unix_epoch_usec(TEST_EPOCH);

  FilterXExpr *lhs = filterx_literal_new(filterx_datetime_new(&lhs_time));
  FilterXExpr *rhs = filterx_literal_new(filterx_double_new(3600.0)); // 1h in sec

  FilterXExpr *expr = filterx_operator_plus_new(lhs, rhs);
  cr_assert_not_null(expr);

  FilterXObject *obj = filterx_expr_eval(expr);
  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(datetime)));

  assert_object_repr_equals(obj, "2020-01-01T01:00:00.000+00:00");

  filterx_object_unref(obj);
  filterx_expr_unref(expr);
}

Test(expr_plus, test_datetime_add_wrong_type)
{
  UnixTime lhs_time = unix_time_from_unix_epoch_usec(TEST_EPOCH);
  FilterXExpr *lhs = filterx_literal_new(filterx_datetime_new(&lhs_time));
  FilterXExpr *rhs = filterx_literal_new(filterx_null_new());

  FilterXExpr *expr = filterx_operator_plus_new(lhs, rhs);
  cr_assert_not_null(expr);

  FilterXObject *obj = filterx_expr_eval(expr);
  cr_assert_null(obj);
  filterx_expr_unref(expr);
}

Test(expr_plus, test_integer_add_integer)
{
  FilterXExpr *lhs = filterx_literal_new(filterx_integer_new(33));
  FilterXExpr *rhs = filterx_literal_new(filterx_integer_new(66));

  FilterXExpr *expr = filterx_operator_plus_new(lhs, rhs);
  cr_assert_not_null(expr);

  FilterXObject *obj = filterx_expr_eval(expr);
  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(integer)));

  GenericNumber gn = filterx_primitive_get_value(obj);

  cr_assert_eq(gn_as_int64(&gn), 99);

  filterx_object_unref(obj);
  filterx_expr_unref(expr);
}

Test(expr_plus, test_integer_add_double)
{
  FilterXExpr *lhs = filterx_literal_new(filterx_integer_new(33));
  FilterXExpr *rhs = filterx_literal_new(filterx_double_new(0.66));

  FilterXExpr *expr = filterx_operator_plus_new(lhs, rhs);
  cr_assert_not_null(expr);

  FilterXObject *obj = filterx_expr_eval(expr);
  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(double)));

  GenericNumber gn = filterx_primitive_get_value(obj);
  GenericNumber expected;
  gn_set_double(&expected, 33 + .66, 0);

  cr_assert(gn_compare(&expected, &gn) == 0);

  filterx_object_unref(obj);
  filterx_expr_unref(expr);
}

Test(expr_plus, test_integer_add_wrong_type)
{
  FilterXExpr *lhs = filterx_literal_new(filterx_integer_new(33));
  FilterXExpr *rhs = filterx_literal_new(filterx_null_new());

  FilterXExpr *expr = filterx_operator_plus_new(lhs, rhs);
  cr_assert_not_null(expr);

  FilterXObject *obj = filterx_expr_eval(expr);
  cr_assert_null(obj);
  filterx_expr_unref(expr);
}

Test(expr_plus, test_double_add_double)
{
  FilterXExpr *lhs = filterx_literal_new(filterx_double_new(.6));
  FilterXExpr *rhs = filterx_literal_new(filterx_double_new(3.1415));

  FilterXExpr *expr = filterx_operator_plus_new(lhs, rhs);
  cr_assert_not_null(expr);

  FilterXObject *obj = filterx_expr_eval(expr);
  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(double)));

  GenericNumber gn = filterx_primitive_get_value(obj);
  GenericNumber expected;
  gn_set_double(&expected, 3.1415 + .6, 0);

  cr_assert(gn_compare(&expected, &gn) == 0);

  filterx_object_unref(obj);
  filterx_expr_unref(expr);
}

Test(expr_plus, test_double_add_integer)
{
  FilterXExpr *lhs = filterx_literal_new(filterx_double_new(0.66));
  FilterXExpr *rhs = filterx_literal_new(filterx_integer_new(33));

  FilterXExpr *expr = filterx_operator_plus_new(lhs, rhs);
  cr_assert_not_null(expr);

  FilterXObject *obj = filterx_expr_eval(expr);
  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(double)));

  GenericNumber gn = filterx_primitive_get_value(obj);
  GenericNumber expected;
  gn_set_double(&expected, .66 + 33, 0);

  cr_assert(gn_compare(&expected, &gn) == 0);

  filterx_object_unref(obj);
  filterx_expr_unref(expr);
}


Test(expr_plus, test_double_add_wrong_type)
{
  FilterXExpr *lhs = filterx_literal_new(filterx_double_new(.5));
  FilterXExpr *rhs = filterx_literal_new(filterx_null_new());

  FilterXExpr *expr = filterx_operator_plus_new(lhs, rhs);
  cr_assert_not_null(expr);

  FilterXObject *obj = filterx_expr_eval(expr);
  cr_assert_null(obj);
  filterx_expr_unref(expr);
}

Test(expr_plus, test_list_add_list)
{
  FilterXExpr *lhs = filterx_literal_new(filterx_object_from_json("[\"foo\",\"bar\"]", -1, NULL));
  FilterXExpr *rhs = filterx_literal_new(filterx_object_from_json("[\"tik\",\"tak\"]", -1, NULL));

  FilterXExpr *expr = filterx_operator_plus_new(lhs, rhs);
  cr_assert_not_null(expr);

  FilterXObject *obj = filterx_expr_eval(expr);
  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(list)));

  assert_object_repr_equals(obj, "[\"foo\",\"bar\",\"tik\",\"tak\"]");

  filterx_object_unref(obj);
  filterx_expr_unref(expr);
}


Test(expr_plus, test_list_add_wrong_type)
{
  FilterXExpr *lhs = filterx_literal_new(filterx_object_from_json("[\"foo\",\"bar\"]", -1, NULL));
  FilterXExpr *rhs = filterx_literal_new(filterx_null_new());

  FilterXExpr *expr = filterx_operator_plus_new(lhs, rhs);
  cr_assert_not_null(expr);

  FilterXObject *obj = filterx_expr_eval(expr);
  cr_assert_null(obj);
  filterx_expr_unref(expr);
}

Test(expr_plus, test_dict_add_dict)
{
  FilterXExpr *lhs = filterx_literal_new(filterx_object_from_json("{\"foo\":\"bar\"}", -1, NULL));
  FilterXExpr *rhs = filterx_literal_new(filterx_object_from_json("{\"tik\":\"tak\"}", -1, NULL));

  FilterXExpr *expr = filterx_operator_plus_new(lhs, rhs);
  cr_assert_not_null(expr);

  FilterXObject *obj = filterx_expr_eval(expr);
  cr_assert_not_null(obj);
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(dict)));

  assert_object_repr_equals(obj, "{\"foo\":\"bar\",\"tik\":\"tak\"}");

  filterx_object_unref(obj);
  filterx_expr_unref(expr);
}

Test(expr_plus, test_dict_add_wrong_type)
{
  FilterXExpr *lhs = filterx_literal_new(filterx_object_from_json("{\"foo\":\"bar\"}", -1, NULL));
  FilterXExpr *rhs = filterx_literal_new(filterx_null_new());

  FilterXExpr *expr = filterx_operator_plus_new(lhs, rhs);
  cr_assert_not_null(expr);

  FilterXObject *obj = filterx_expr_eval(expr);
  cr_assert_null(obj);
  filterx_expr_unref(expr);
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


TestSuite(expr_plus, .init = setup, .fini = teardown);
