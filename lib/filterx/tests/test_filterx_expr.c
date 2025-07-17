/*
 * Copyright (c) 2023 Balazs Scheidler <balazs.scheidler@axoflow.com>
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
#include "libtest/cr_template.h"

#include "filterx/filterx-scope.h"
#include "filterx/filterx-eval.h"
#include "filterx/expr-literal.h"
#include "filterx/expr-template.h"
#include "filterx/expr-literal-container.h"
#include "filterx/object-primitive.h"
#include "filterx/object-string.h"
#include "filterx/object-dict.h"
#include "filterx/object-list.h"
#include "filterx/object-list-interface.h"
#include "filterx/object-dict-interface.h"
#include "filterx/expr-assign.h"
#include "filterx/expr-variable.h"
#include "filterx/expr-setattr.h"
#include "filterx/expr-getattr.h"
#include "filterx/expr-set-subscript.h"
#include "filterx/expr-get-subscript.h"

#include "apphook.h"
#include "scratch-buffers.h"

#include "libtest/filterx-lib.h"

static void
_assert_int_value_and_unref(FilterXObject *object, gint64 expected_value)
{
  gint64 value;
  cr_assert(filterx_integer_unwrap(object, &value));
  cr_assert_eq(value, expected_value);
  filterx_object_unref(object);
}

Test(filterx_expr, test_filterx_expr_construction_and_free)
{
  FilterXExpr *fexpr = filterx_expr_new();

  filterx_expr_unref(fexpr);
}

Test(filterx_expr, test_filterx_literal_evaluates_to_the_literal_object)
{
  FilterXExpr *fexpr = filterx_literal_new(filterx_integer_new(42));
  FilterXObject *fobj = init_and_eval_expr(fexpr);

  assert_marshaled_object(fobj, "42", LM_VT_INTEGER);

  filterx_expr_unref(fexpr);
  filterx_object_unref(fobj);
}

Test(filterx_expr, test_filterx_template_evaluates_to_the_expanded_value)
{
  FilterXExpr *fexpr = filterx_template_new(compile_template("$HOST $PROGRAM"));
  FilterXObject *fobj = init_and_eval_expr(fexpr);

  assert_marshaled_object(fobj, "bzorp syslog-ng", LM_VT_STRING);

  filterx_expr_unref(fexpr);
  filterx_object_unref(fobj);
}

Test(filterx_expr, test_filterx_literal_list_with_inmutable_values)
{
  FilterXExpr *list_expr = NULL;
  FilterXObject *result = NULL;
  GList *values = NULL;
  guint64 len;


  // [42];
  values = g_list_append(values,
                         filterx_literal_element_new(NULL,
                                                     filterx_literal_new(filterx_integer_new(42))));
  values = g_list_append(values,
                         filterx_literal_element_new(NULL,
                                                     filterx_literal_new(filterx_integer_new(43))));
  values = g_list_append(values,
                         filterx_literal_element_new(NULL,
                                                     filterx_literal_new(filterx_integer_new(44))));
  list_expr = filterx_literal_list_new(values);

  // result = [42,43,44];
  result = init_and_eval_expr(list_expr);
  cr_assert(result);
  cr_assert(filterx_object_truthy(result));
  cr_assert(filterx_object_len(result, &len));
  cr_assert_eq(len, 3);
  _assert_int_value_and_unref(filterx_list_get_subscript(result, 0), 42);
  _assert_int_value_and_unref(filterx_list_get_subscript(result, 1), 43);
  _assert_int_value_and_unref(filterx_list_get_subscript(result, 2), 44);
  filterx_object_unref(result);
  filterx_expr_unref(list_expr);
}

Test(filterx_expr, test_filterx_literal_list_with_embedded_list)
{
  FilterXExpr *list_expr = NULL;
  FilterXObject *result = NULL;
  GList *values = NULL, *inner_values = NULL;
  guint64 len;

  // [[1337]];
  inner_values = g_list_append(NULL,
                               filterx_literal_element_new(NULL,
                                                           filterx_literal_new(filterx_integer_new(1337))));
  values = g_list_append(NULL,
                         filterx_literal_element_new(NULL,
                                                     filterx_literal_list_new(inner_values)));
  list_expr = filterx_literal_list_new(values);

  // result = [[1337]];
  result = init_and_eval_expr(list_expr);
  cr_assert(result);
  cr_assert(filterx_object_truthy(result));
  cr_assert(filterx_object_len(result, &len));
  cr_assert_eq(len, 1);

  FilterXObject *stored_inner_list = filterx_list_get_subscript(result, 0);
  cr_assert(stored_inner_list);
  cr_assert(filterx_object_is_type(filterx_ref_unwrap_ro(stored_inner_list), &FILTERX_TYPE_NAME(list)));
  cr_assert(filterx_object_len(stored_inner_list, &len));
  cr_assert_eq(len, 1);
  _assert_int_value_and_unref(filterx_list_get_subscript(stored_inner_list, 0), 1337);
  filterx_object_unref(stored_inner_list);

  filterx_object_unref(result);
  filterx_expr_unref(list_expr);
}

Test(filterx_expr, test_filterx_dict_immutable_values)
{
  FilterXExpr *dict_expr = NULL;
  FilterXObject *result = NULL;
  GList *values = NULL;
  guint64 len;

  FilterXObject *foo = filterx_string_new("foo", -1);
  FilterXObject *bar = filterx_string_new("bar", -1);
  FilterXObject *baz = filterx_string_new("baz", -1);


  values = g_list_append(values,
                         filterx_literal_element_new(filterx_literal_new(filterx_object_ref(foo)),
                                                     filterx_literal_new(filterx_integer_new(42))));
  values = g_list_append(values,
                         filterx_literal_element_new(filterx_literal_new(filterx_object_ref(bar)),
                                                     filterx_literal_new(filterx_integer_new(43))));
  values = g_list_append(values,
                         filterx_literal_element_new(filterx_literal_new(filterx_object_ref(baz)),
                                                     filterx_literal_new(filterx_integer_new(44))));
  dict_expr = filterx_literal_dict_new(values);

  // result = {"foo": 42, "bar": 43, "baz": 44};
  result = init_and_eval_expr(dict_expr);
  cr_assert(result);
  cr_assert(filterx_object_truthy(result));
  cr_assert(filterx_object_len(result, &len));
  cr_assert_eq(len, 3);
  _assert_int_value_and_unref(filterx_object_get_subscript(result, foo), 42);
  _assert_int_value_and_unref(filterx_object_get_subscript(result, bar), 43);
  _assert_int_value_and_unref(filterx_object_get_subscript(result, baz), 44);
  filterx_object_unref(result);
  filterx_object_unref(baz);
  filterx_object_unref(bar);
  filterx_object_unref(foo);
  filterx_expr_unref(dict_expr);
}

Test(filterx_expr, test_filterx_dict_with_embedded_dict)
{
  FilterXExpr *dict_expr = NULL;
  FilterXObject *result = NULL;
  GList *values = NULL, *inner_values = NULL;
  guint64 len;

  FilterXObject *foo = filterx_string_new("foo", -1);
  FilterXObject *bar = filterx_string_new("bar", -1);
  FilterXObject *baz = filterx_string_new("baz", -1);

  // {"foo": 1};
  inner_values = g_list_append(inner_values,
                               filterx_literal_element_new(filterx_literal_new(filterx_object_ref(foo)),
                                                           filterx_literal_new(filterx_integer_new(1))));
  // result = {"foo": 420, "bar": 1337", "baz": {"foo":1}};
  values = g_list_append(values,
                         filterx_literal_element_new(filterx_literal_new(filterx_object_ref(foo)),
                                                     filterx_literal_new(filterx_integer_new(420))));
  values = g_list_append(values,
                         filterx_literal_element_new(filterx_literal_new(filterx_object_ref(bar)),
                                                     filterx_literal_new(filterx_integer_new(1337))));
  values = g_list_append(values,
                         filterx_literal_element_new(filterx_literal_new(filterx_object_ref(baz)),
                                                     filterx_literal_dict_new(inner_values)));

  dict_expr = filterx_literal_dict_new(values);

  result = init_and_eval_expr(dict_expr);
  cr_assert(result);
  cr_assert(filterx_object_truthy(result));
  cr_assert(filterx_object_len(result, &len));
  cr_assert_eq(len, 3);
  _assert_int_value_and_unref(filterx_object_get_subscript(result, foo), 420);
  _assert_int_value_and_unref(filterx_object_get_subscript(result, bar), 1337);

  FilterXObject *stored_inner_dict = filterx_object_get_subscript(result, baz);
  cr_assert(stored_inner_dict);
  cr_assert(filterx_object_is_type(filterx_ref_unwrap_ro(stored_inner_dict), &FILTERX_TYPE_NAME(dict)));
  cr_assert_eq(filterx_object_len(stored_inner_dict, &len), 1);
  _assert_int_value_and_unref(filterx_object_get_subscript(stored_inner_dict, foo), 1);
  filterx_object_unref(stored_inner_dict);

  filterx_object_unref(result);
  filterx_expr_unref(dict_expr);

  filterx_object_unref(baz);
  filterx_object_unref(bar);
  filterx_object_unref(foo);
}

Test(filterx_expr, test_filterx_assign)
{
  FilterXExpr *result_var = filterx_msg_variable_expr_new("result_var");
  cr_assert(result_var != NULL);

  FilterXExpr *assign = filterx_assign_new(result_var, filterx_literal_new(filterx_string_new("foobar", -1)));

  FilterXObject *res = init_and_eval_expr(assign);
  cr_assert_not_null(res);
  cr_assert(filterx_object_is_type(res, &FILTERX_TYPE_NAME(string)));
  cr_assert_str_eq(filterx_string_get_value_ref(res, NULL), "foobar");
  cr_assert(filterx_object_truthy(res));
  cr_assert(assign->ignore_falsy_result);

  cr_assert_not_null(result_var);
  FilterXObject *result_obj = init_and_eval_expr(result_var);
  cr_assert_not_null(result_obj);
  cr_assert(filterx_object_is_type(result_obj, &FILTERX_TYPE_NAME(string)));
  const gchar *result_val = filterx_string_get_value_ref(result_obj, NULL);
  cr_assert_str_eq("foobar", result_val);

  filterx_object_unref(res);
  filterx_expr_unref(assign);
  filterx_object_unref(result_obj);
}

Test(filterx_expr, test_filterx_setattr)
{
  FilterXObject *dict = filterx_dict_new();
  FilterXExpr *fillable = filterx_literal_new(dict);

  FilterXExpr *setattr = filterx_setattr_new(fillable, filterx_string_new("foo", -1),
                                             filterx_literal_new(filterx_string_new("bar", -1)));
  cr_assert_not_null(setattr);

  FilterXObject *res = init_and_eval_expr(setattr);
  cr_assert_not_null(res);
  cr_assert(filterx_object_is_type(res, &FILTERX_TYPE_NAME(string)));
  cr_assert_str_eq(filterx_string_get_value_ref(res, NULL), "bar");
  cr_assert(filterx_object_truthy(res));
  cr_assert(setattr->ignore_falsy_result);

  assert_object_json_equals(dict, "{\"foo\":\"bar\"}");

  filterx_expr_unref(setattr);
}

Test(filterx_expr, test_filterx_set_subscript)
{
  FilterXObject *dict = filterx_dict_new();
  FilterXExpr *fillable = filterx_literal_new(dict);

  FilterXExpr *setattr = filterx_set_subscript_new(fillable,
                                                   filterx_literal_new(filterx_string_new("foo", -1)),
                                                   filterx_literal_new(filterx_string_new("bar", -1)));
  cr_assert_not_null(setattr);

  FilterXObject *res = init_and_eval_expr(setattr);
  cr_assert_not_null(res);
  cr_assert(filterx_object_is_type(res, &FILTERX_TYPE_NAME(string)));
  cr_assert_str_eq(filterx_string_get_value_ref(res, NULL), "bar");
  cr_assert(filterx_object_truthy(res));
  cr_assert(setattr->ignore_falsy_result);

  assert_object_json_equals(dict, "{\"foo\":\"bar\"}");

  filterx_expr_unref(setattr);
}

static gboolean
_is_string_in_filterx_eval_errors(const gchar *error_str)
{
  gint error_count = filterx_eval_get_error_count();
  if (!error_count)
    return FALSE;

  for (gint i = 0; i < error_count; i++)
    {
      if (strstr(filterx_eval_get_error(i), error_str))
        return TRUE;
    }

  return FALSE;
}

Test(filterx_expr, test_filterx_readonly)
{
  FilterXObject *foo = filterx_string_new("foo", -1);
  FilterXObject *bar = filterx_string_new("bar", -1);

  FilterXObject *dict = filterx_test_dict_new();

  FilterXObject *inner_dict = filterx_test_dict_new();
  cr_assert(filterx_object_set_subscript(dict, foo, &inner_dict));
  filterx_object_unref(inner_dict);

  filterx_object_make_readonly(dict);

  FilterXExpr *literal = filterx_literal_new(dict);


  FilterXExpr *setattr = filterx_setattr_new(filterx_expr_ref(literal),
                                             filterx_string_new("bar", -1),
                                             filterx_literal_new(filterx_object_ref(foo)));
  cr_assert_not(init_and_eval_expr(setattr));
  cr_assert(_is_string_in_filterx_eval_errors("readonly"));
  filterx_eval_clear_errors();
  filterx_expr_unref(setattr);


  FilterXExpr *set_subscript = filterx_set_subscript_new(filterx_expr_ref(literal),
                                                         filterx_literal_new(filterx_object_ref(bar)),
                                                         filterx_literal_new(filterx_object_ref(foo)));
  cr_assert_not(init_and_eval_expr(set_subscript));
  cr_assert(_is_string_in_filterx_eval_errors("readonly"));
  filterx_eval_clear_errors();
  filterx_expr_unref(set_subscript);


  FilterXExpr *getattr = filterx_getattr_new(filterx_expr_ref(literal), filterx_string_new("foo", -1));
  cr_assert_not(filterx_expr_unset(getattr));
  cr_assert(_is_string_in_filterx_eval_errors("readonly"));
  filterx_eval_clear_errors();
  filterx_expr_unref(getattr);


  FilterXExpr *get_subscript = filterx_get_subscript_new(filterx_expr_ref(literal),
                                                         filterx_literal_new(filterx_object_ref(foo)));
  cr_assert_not(filterx_expr_unset(get_subscript));
  cr_assert(_is_string_in_filterx_eval_errors("readonly"));
  filterx_eval_clear_errors();
  filterx_expr_unref(get_subscript);


  FilterXExpr *inner = filterx_setattr_new(filterx_getattr_new(filterx_expr_ref(literal),
                                           filterx_string_new("foo", -1)),
                                           filterx_string_new("bar", -1),
                                           filterx_literal_new(filterx_object_ref(bar)));
  cr_assert_not(init_and_eval_expr(inner));
  cr_assert(_is_string_in_filterx_eval_errors("readonly"));
  filterx_eval_clear_errors();
  filterx_expr_unref(inner);


  filterx_expr_unref(literal);
  filterx_object_unref(bar);
  filterx_object_unref(foo);
}

static void
setup(void)
{
  app_startup();
  init_template_tests();
  init_libtest_filterx();
}

static void
teardown(void)
{
  deinit_libtest_filterx();
  scratch_buffers_explicit_gc();
  deinit_template_tests();
  app_shutdown();
}

TestSuite(filterx_expr, .init = setup, .fini = teardown);
