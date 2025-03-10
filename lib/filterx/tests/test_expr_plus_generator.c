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
#include "filterx/expr-plus-generator.h"
#include "filterx/object-json.h"
#include "filterx/object-list-interface.h"
#include "filterx/object-dict-interface.h"
#include "filterx/expr-generator.h"
#include "filterx/expr-literal-generator.h"

#include "apphook.h"
#include "scratch-buffers.h"

void
_assert_cmp_lists(FilterXObject *expected, FilterXObject *provided)
{
  cr_assert(filterx_object_is_type(expected, &FILTERX_TYPE_NAME(list)));
  cr_assert(filterx_object_is_type(provided, &FILTERX_TYPE_NAME(list)));
  guint64 expected_len, provided_len;
  cr_assert(filterx_object_len(expected, &expected_len));
  cr_assert(filterx_object_len(provided, &provided_len));
  cr_assert(expected_len == provided_len, "expected len:%lu provided len:%lu", expected_len, provided_len);
  for (guint64 i = 0; i < expected_len; i ++)
    {
      FilterXObject *expected_value_obj = filterx_list_get_subscript(expected, i);
      FilterXObject *provided_value_obj = filterx_list_get_subscript(provided, i);
      cr_assert(expected_value_obj->type == provided_value_obj->type, "expected type:%s provided type:%s",
                expected_value_obj->type->name, provided_value_obj->type->name);

      GString *expected_val = scratch_buffers_alloc();
      GString *provided_val = scratch_buffers_alloc();
      cr_assert(filterx_object_repr(expected_value_obj, expected_val), "repr call failure on expected val");
      cr_assert(filterx_object_repr(provided_value_obj, provided_val), "repr call failure on provided val");

      cr_assert_str_eq(expected_val->str, provided_val->str, "values of nth(%lu) elt differs: expected:%s provided:%s", i,
                       expected_val->str, provided_val->str);

      filterx_object_unref(expected_value_obj);
      filterx_object_unref(provided_value_obj);
    }
}

Test(expr_plus_generator, test_list_add_two_generators_with_post_set_fillable)
{
  FilterXExpr *lhs = filterx_literal_list_generator_new();
  GList *lhs_vals = NULL;
  lhs_vals = g_list_append(lhs_vals,
                           filterx_literal_generator_elem_new(NULL, filterx_literal_new(filterx_string_new("foo", -1)), FALSE));
  lhs_vals = g_list_append(lhs_vals,
                           filterx_literal_generator_elem_new(NULL, filterx_literal_new(filterx_string_new("bar", -1)), FALSE));
  filterx_literal_generator_set_elements(lhs, lhs_vals);

  FilterXExpr *rhs = filterx_literal_list_generator_new();
  GList *rhs_vals = NULL;
  rhs_vals = g_list_append(rhs_vals,
                           filterx_literal_generator_elem_new(NULL, filterx_literal_new(filterx_string_new("baz", -1)), FALSE));
  rhs_vals = g_list_append(rhs_vals,
                           filterx_literal_generator_elem_new(NULL, filterx_literal_new(filterx_string_new("other", -1)), FALSE));
  filterx_literal_generator_set_elements(rhs, rhs_vals);


  FilterXExpr *expr = filterx_operator_plus_generator_new(lhs, rhs);
  FilterXObject *json_array = filterx_json_array_new_empty();
  FilterXExpr *fillable = filterx_literal_new(json_array);
  filterx_generator_set_fillable(expr, filterx_expr_ref(fillable));

  FilterXObject *res_object = filterx_expr_eval(expr);
  cr_assert_not_null(res_object);
  cr_assert(filterx_object_is_type(res_object, &FILTERX_TYPE_NAME(list)));

  FilterXObject *expected = filterx_json_array_new_from_repr("[\"foo\",\"bar\",\"baz\",\"other\"]", -1);

  _assert_cmp_lists(expected, res_object);

  filterx_expr_unref(expr);
  filterx_expr_unref(fillable);
  filterx_object_unref(expected);
  filterx_object_unref(json_array);
}

Test(expr_plus_generator, test_list_add_variable_to_generator_with_post_set_fillable)
{
  FilterXExpr *lhs = filterx_literal_list_generator_new();
  GList *lhs_vals = NULL;
  lhs_vals = g_list_append(lhs_vals,
                           filterx_literal_generator_elem_new(NULL, filterx_literal_new(filterx_string_new("foo", -1)), FALSE));
  lhs_vals = g_list_append(lhs_vals,
                           filterx_literal_generator_elem_new(NULL, filterx_literal_new(filterx_string_new("bar", -1)), FALSE));
  filterx_literal_generator_set_elements(lhs, lhs_vals);

  FilterXExpr *rhs = filterx_non_literal_new(filterx_json_array_new_from_repr("[\"baz\", \"other\"]", -1));

  FilterXExpr *expr = filterx_operator_plus_generator_new(lhs, rhs);
  FilterXObject *json_array = filterx_json_array_new_empty();
  FilterXExpr *fillable = filterx_literal_new(json_array);
  filterx_generator_set_fillable(expr, filterx_expr_ref(fillable));

  FilterXObject *res_object = filterx_expr_eval(expr);
  cr_assert_not_null(res_object);
  cr_assert(filterx_object_is_type(res_object, &FILTERX_TYPE_NAME(list)));

  FilterXObject *expected = filterx_json_array_new_from_repr("[\"foo\",\"bar\",\"baz\",\"other\"]", -1);

  _assert_cmp_lists(expected, res_object);

  filterx_expr_unref(expr);
  filterx_expr_unref(fillable);
  filterx_object_unref(expected);
  filterx_object_unref(json_array);
}

Test(expr_plus_generator, test_list_add_generator_to_variable_with_post_set_fillable)
{
  FilterXExpr *lhs = filterx_non_literal_new(filterx_json_array_new_from_repr("[\"foo\", \"bar\"]", -1));

  FilterXExpr *rhs = filterx_literal_list_generator_new();
  GList *rhs_vals = NULL;
  rhs_vals = g_list_append(rhs_vals,
                           filterx_literal_generator_elem_new(NULL, filterx_literal_new(filterx_string_new("baz", -1)), FALSE));
  rhs_vals = g_list_append(rhs_vals,
                           filterx_literal_generator_elem_new(NULL, filterx_literal_new(filterx_string_new("other", -1)), FALSE));
  filterx_literal_generator_set_elements(rhs, rhs_vals);

  FilterXExpr *expr = filterx_operator_plus_generator_new(lhs, rhs);
  FilterXObject *json_array = filterx_json_array_new_empty();
  FilterXExpr *fillable = filterx_literal_new(json_array);
  filterx_generator_set_fillable(expr, filterx_expr_ref(fillable));

  FilterXObject *res_object = filterx_expr_eval(expr);
  cr_assert_not_null(res_object);
  cr_assert(filterx_object_is_type(res_object, &FILTERX_TYPE_NAME(list)));

  FilterXObject *expected = filterx_json_array_new_from_repr("[\"foo\",\"bar\",\"baz\",\"other\"]", -1);

  _assert_cmp_lists(expected, res_object);

  filterx_expr_unref(expr);
  filterx_expr_unref(fillable);
  filterx_object_unref(expected);
  filterx_object_unref(json_array);
}


Test(expr_plus_generator, test_nested_dict_add_two_generators_with_post_set_fillable)
{
  FilterXExpr *lhs = filterx_literal_dict_generator_new();
  GList *lhs_vals = NULL;
  GList *lhs_inner = NULL;
  lhs_inner = g_list_append(lhs_inner,
                            filterx_literal_generator_elem_new(filterx_literal_new(filterx_string_new("bar", -1)),
                                filterx_literal_new(filterx_string_new("baz", -1)), TRUE));
  lhs_vals = g_list_append(lhs_vals,
                           filterx_literal_generator_elem_new(filterx_literal_new(filterx_string_new("foo", -1)),
                                                              filterx_literal_inner_dict_generator_new(lhs, lhs_inner), FALSE));
  filterx_literal_generator_set_elements(lhs, lhs_vals);

  FilterXExpr *rhs = filterx_literal_dict_generator_new();
  GList *rhs_vals = NULL;
  GList *rhs_inner = NULL;
  rhs_inner = g_list_append(rhs_inner,
                            filterx_literal_generator_elem_new(filterx_literal_new(filterx_string_new("tak", -1)),
                                filterx_literal_new(filterx_string_new("toe", -1)), TRUE));
  rhs_vals = g_list_append(rhs_vals,
                           filterx_literal_generator_elem_new(filterx_literal_new(filterx_string_new("tik", -1)),
                                                              filterx_literal_inner_dict_generator_new(rhs, rhs_inner), FALSE));
  filterx_literal_generator_set_elements(rhs, rhs_vals);

  FilterXExpr *expr = filterx_operator_plus_generator_new(lhs, rhs);
  FilterXObject *json_dict = filterx_json_object_new_empty();
  FilterXExpr *fillable = filterx_literal_new(json_dict);
  filterx_generator_set_fillable(expr, filterx_expr_ref(fillable));

  FilterXObject *res_object = filterx_expr_eval(expr);
  cr_assert_not_null(res_object);
  cr_assert(filterx_object_is_type(res_object, &FILTERX_TYPE_NAME(dict)));

  assert_object_json_equals(res_object, "{\"foo\":{\"bar\":\"baz\"},\"tik\":{\"tak\":\"toe\"}}");

  filterx_expr_unref(expr);
  filterx_expr_unref(fillable);
  filterx_object_unref(json_dict);
}

Test(expr_plus_generator, test_nested_dict_add_variable_to_generator_with_post_set_fillable)
{
  FilterXExpr *lhs = filterx_literal_dict_generator_new();
  GList *lhs_vals = NULL;
  GList *lhs_inner = NULL;
  lhs_inner = g_list_append(lhs_inner,
                            filterx_literal_generator_elem_new(filterx_literal_new(filterx_string_new("bar", -1)),
                                filterx_literal_new(filterx_string_new("baz", -1)), TRUE));
  lhs_vals = g_list_append(lhs_vals,
                           filterx_literal_generator_elem_new(filterx_literal_new(filterx_string_new("foo", -1)),
                                                              filterx_literal_inner_dict_generator_new(lhs, lhs_inner), FALSE));
  filterx_literal_generator_set_elements(lhs, lhs_vals);

  FilterXExpr *rhs = filterx_non_literal_new(filterx_json_object_new_from_repr("{\"tik\":{\"tak\":\"toe\"}}", -1));

  FilterXExpr *expr = filterx_operator_plus_generator_new(lhs, rhs);
  FilterXObject *json_dict = filterx_json_object_new_empty();
  FilterXExpr *fillable = filterx_literal_new(json_dict);
  filterx_generator_set_fillable(expr, filterx_expr_ref(fillable));

  FilterXObject *res_object = filterx_expr_eval(expr);
  cr_assert_not_null(res_object);
  cr_assert(filterx_object_is_type(res_object, &FILTERX_TYPE_NAME(dict)));

  struct json_object *jso = NULL;
  FilterXObject *assoc_object = NULL;
  cr_assert(filterx_object_map_to_json(res_object, &jso, &assoc_object));

  const gchar *json_repr = json_object_to_json_string_ext(jso, JSON_C_TO_STRING_PLAIN);
  cr_assert_str_eq(json_repr, "{\"foo\":{\"bar\":\"baz\"},\"tik\":{\"tak\":\"toe\"}}");
  json_object_put(jso);
  filterx_object_unref(assoc_object);

  filterx_expr_unref(expr);
  filterx_expr_unref(fillable);
  filterx_object_unref(json_dict);
}

Test(expr_plus_generator, test_nested_dict_add_generator_to_variable_with_post_set_fillable)
{

  FilterXExpr *lhs = filterx_non_literal_new(filterx_json_object_new_from_repr("{\"foo\":{\"bar\":\"baz\"}}", -1));

  FilterXExpr *rhs = filterx_literal_dict_generator_new();
  GList *rhs_vals = NULL;
  GList *rhs_inner = NULL;
  rhs_inner = g_list_append(rhs_inner,
                            filterx_literal_generator_elem_new(filterx_literal_new(filterx_string_new("tak", -1)),
                                filterx_literal_new(filterx_string_new("toe", -1)), TRUE));
  rhs_vals = g_list_append(rhs_vals,
                           filterx_literal_generator_elem_new(filterx_literal_new(filterx_string_new("tik", -1)),
                                                              filterx_literal_inner_dict_generator_new(rhs, rhs_inner), FALSE));
  filterx_literal_generator_set_elements(rhs, rhs_vals);

  FilterXExpr *expr = filterx_operator_plus_generator_new(lhs, rhs);
  FilterXObject *json_dict = filterx_json_object_new_empty();
  FilterXExpr *fillable = filterx_literal_new(json_dict);
  filterx_generator_set_fillable(expr, filterx_expr_ref(fillable));

  FilterXObject *res_object = filterx_expr_eval(expr);
  cr_assert_not_null(res_object);
  cr_assert(filterx_object_is_type(res_object, &FILTERX_TYPE_NAME(dict)));

  struct json_object *jso = NULL;
  FilterXObject *assoc_object = NULL;
  cr_assert(filterx_object_map_to_json(res_object, &jso, &assoc_object));

  const gchar *json_repr = json_object_to_json_string_ext(jso, JSON_C_TO_STRING_PLAIN);
  cr_assert_str_eq(json_repr, "{\"foo\":{\"bar\":\"baz\"},\"tik\":{\"tak\":\"toe\"}}");
  json_object_put(jso);
  filterx_object_unref(assoc_object);

  filterx_expr_unref(expr);
  filterx_expr_unref(fillable);
  filterx_object_unref(json_dict);
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


TestSuite(expr_plus_generator, .init = setup, .fini = teardown);
