/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2023 shifter <shifter@axoflow.com>
 * Copyright (c) 2024 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#include "filterx/object-string.h"
#include "filterx/expr-compound.h"
#include "filterx/expr-variable.h"
#include "filterx/expr-assign.h"
#include "filterx/expr-literal.h"
#include "apphook.h"
#include "scratch-buffers.h"

/* FIXME: these were copied from test_expr_condition and should be moved to libtest */
FilterXExpr *
_assert_assign_var(const char *var_name, FilterXExpr *value)
{
  FilterXExpr *control_variable = filterx_msg_variable_expr_new(filterx_string_typed_new(var_name));
  cr_assert(control_variable != NULL);

  return filterx_assign_new(control_variable, value);
}

FilterXExpr *
_string_to_filterXExpr(const gchar *str)
{
  return filterx_literal_new(filterx_string_new(str, -1));
}

gint
_assert_cmp_string_to_filterx_object(const char *str, FilterXObject *obj)
{
  cr_assert(filterx_object_is_type(obj, &FILTERX_TYPE_NAME(string)));
  gsize string_len;
  const gchar *string = filterx_string_get_value_ref(obj, &string_len);
  return strcmp(string, str);
}

void
_assert_set_test_variable(const char *var_name, FilterXExpr *expr)
{
  FilterXExpr *assign = _assert_assign_var(var_name, expr);
  cr_assert(assign != NULL);

  FilterXObject *assign_eval_res = filterx_expr_eval(assign);
  cr_assert(assign_eval_res != NULL);
  cr_assert(filterx_object_truthy(assign_eval_res));

  filterx_expr_unref(assign);
  filterx_object_unref(assign_eval_res);
}

FilterXObject *
_assert_get_test_variable(const char *var_name)
{
  FilterXExpr *control_variable = filterx_msg_variable_expr_new(filterx_string_typed_new(var_name));
  cr_assert(control_variable != NULL);
  FilterXObject *result = filterx_expr_eval(control_variable);
  filterx_expr_unref(control_variable);
  return result;
}


Test(expr_compound, test_compound_all_the_statements_must_execute_and_return_truthy)
{
  FilterXExpr *compound = filterx_compound_expr_new_va(FALSE,
                                                       _assert_assign_var("$control-value", _string_to_filterXExpr("matching")),
                                                       _assert_assign_var("$control-value2", _string_to_filterXExpr("matching2")),
                                                       _assert_assign_var("$control-value3", _string_to_filterXExpr("matching3")),
                                                       NULL);


  FilterXObject *res = filterx_expr_eval(compound);
  cr_assert(res != NULL);
  cr_assert(filterx_object_truthy(res));
  filterx_object_unref(res);

  FilterXObject *control_value = _assert_get_test_variable("$control-value");
  cr_assert_eq(0, _assert_cmp_string_to_filterx_object("matching", control_value));
  filterx_object_unref(control_value);
  control_value = _assert_get_test_variable("$control-value2");
  cr_assert_eq(0, _assert_cmp_string_to_filterx_object("matching2", control_value));
  filterx_object_unref(control_value);
  control_value = _assert_get_test_variable("$control-value3");
  cr_assert_eq(0, _assert_cmp_string_to_filterx_object("matching3", control_value));
  filterx_object_unref(control_value);

  filterx_expr_unref(compound);
}

Test(expr_compound, test_compound_all_the_statements_must_execute_and_return_the_last_value_if_instructed)
{
  FilterXExpr *compound = filterx_compound_expr_new_va(TRUE,
                                                       _assert_assign_var("$control-value", _string_to_filterXExpr("matching")),
                                                       _assert_assign_var("$control-value2", _string_to_filterXExpr("matching2")),
                                                       _assert_assign_var("$control-value3", _string_to_filterXExpr("matching3")),
                                                       NULL);


  FilterXObject *res = filterx_expr_eval(compound);
  cr_assert(res != NULL);
  cr_assert(filterx_object_truthy(res));
  cr_assert_eq(0, _assert_cmp_string_to_filterx_object("matching3", res));
  filterx_object_unref(res);

  FilterXObject *control_value = _assert_get_test_variable("$control-value");
  cr_assert_eq(0, _assert_cmp_string_to_filterx_object("matching", control_value));
  filterx_object_unref(control_value);
  control_value = _assert_get_test_variable("$control-value2");
  cr_assert_eq(0, _assert_cmp_string_to_filterx_object("matching2", control_value));
  filterx_object_unref(control_value);
  control_value = _assert_get_test_variable("$control-value3");
  cr_assert_eq(0, _assert_cmp_string_to_filterx_object("matching3", control_value));
  filterx_object_unref(control_value);

  filterx_expr_unref(compound);
}


static void
setup(void)
{
  app_startup();
  init_libtest_filterx();
  _assert_set_test_variable("$control-value", _string_to_filterXExpr("default"));
  _assert_set_test_variable("$control-value3", _string_to_filterXExpr("default3"));
}

static void
teardown(void)
{
  scratch_buffers_explicit_gc();
  deinit_libtest_filterx();
  app_shutdown();
}

TestSuite(expr_compound, .init = setup, .fini = teardown);
