/*
 * Copyright (c) 2023 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#include "filterx/object-dict.h"
#include "filterx/object-list.h"
#include "filterx/object-string.h"
#include "filterx/object-message-value.h"
#include "filterx/expr-function.h"
#include "filterx/json-repr.h"
#include "apphook.h"
#include "scratch-buffers.h"

Test(filterx_dict, test_filterx_object_dict_from_repr)
{
  FilterXObject *fobj;

  fobj = filterx_object_from_json("{\"foo\": \"foovalue\"}", -1, NULL);
  cr_assert(filterx_object_is_type_or_ref(fobj, &FILTERX_TYPE_NAME(dict_object)));
  assert_object_json_equals(fobj, "{\"foo\":\"foovalue\"}");
  assert_marshaled_object(fobj, "{\"foo\":\"foovalue\"}", LM_VT_JSON);
  filterx_object_unref(fobj);

  fobj = filterx_object_from_json("[\"foo\", \"bar\"]", -1, NULL);
  cr_assert(filterx_object_is_type_or_ref(fobj, &FILTERX_TYPE_NAME(list_object)));
  assert_object_json_equals(fobj, "[\"foo\",\"bar\"]");
  assert_marshaled_object(fobj, "foo,bar", LM_VT_LIST);
  filterx_object_unref(fobj);

  fobj = filterx_object_from_json("[1, 2]", -1, NULL);
  cr_assert(filterx_object_is_type_or_ref(fobj, &FILTERX_TYPE_NAME(list_object)));
  assert_object_json_equals(fobj, "[1,2]");
  assert_marshaled_object(fobj, "[1,2]", LM_VT_JSON);
  filterx_object_unref(fobj);

  fobj = filterx_list_new_from_syslog_ng_list("\"foo\",bar", -1);
  cr_assert(filterx_object_is_type_or_ref(fobj, &FILTERX_TYPE_NAME(list_object)));
  assert_object_json_equals(fobj, "[\"foo\",\"bar\"]");
  assert_marshaled_object(fobj, "foo,bar", LM_VT_LIST);
  filterx_object_unref(fobj);

  fobj = filterx_list_new_from_syslog_ng_list("1,2", -1);
  cr_assert(filterx_object_is_type_or_ref(fobj, &FILTERX_TYPE_NAME(list_object)));
  assert_object_json_equals(fobj, "[\"1\",\"2\"]");
  assert_marshaled_object(fobj, "1,2", LM_VT_LIST);
  filterx_object_unref(fobj);

  fobj = filterx_object_from_json("[1, \"foo\"]", -1, NULL);
  cr_assert(filterx_object_is_type_or_ref(fobj, &FILTERX_TYPE_NAME(list_object)));
  assert_object_json_equals(fobj, "[1,\"foo\"]");
  assert_marshaled_object(fobj, "[1,\"foo\"]", LM_VT_JSON);
  filterx_object_unref(fobj);
}

static FilterXObject *
_exec_func(FilterXSimpleFunctionProto func, FilterXObject *arg)
{
  if (!arg)
    return func(NULL, NULL, 0);

  FilterXObject *args[] = { arg };
  FilterXObject *result = func(NULL, args, G_N_ELEMENTS(args));
  filterx_object_unref(arg);
  return result;
}

static FilterXObject *
_exec_dict_func(FilterXObject *arg)
{
  return _exec_func(filterx_dict_new_from_args, arg);
}

static FilterXObject *
_exec_list_func(FilterXObject *arg)
{
  return _exec_func(filterx_list_new_from_args, arg);
}

Test(filterx_dict, test_dict_function)
{
  FilterXObject *fobj;

  fobj = _exec_dict_func(NULL);
  cr_assert(filterx_object_is_type_or_ref(fobj, &FILTERX_TYPE_NAME(dict_object)));
  assert_object_json_equals(fobj, "{}");
  filterx_object_unref(fobj);

  fobj = _exec_dict_func(filterx_string_new("{\"foo\": 1}", -1));
  cr_assert(filterx_object_is_type_or_ref(fobj, &FILTERX_TYPE_NAME(dict_object)));
  assert_object_json_equals(fobj, "{\"foo\":1}");
  filterx_object_unref(fobj);

  fobj = _exec_dict_func(filterx_message_value_new("{\"foo\": 1}", -1, LM_VT_JSON));
  cr_assert(fobj == NULL);

  fobj = _exec_dict_func(filterx_message_value_new("{\"foo\": 1}", -1, LM_VT_STRING));
  cr_assert(fobj == NULL);

  fobj = _exec_dict_func(filterx_object_from_json("{\"foo\": 1}", -1, NULL));
  cr_assert(filterx_object_is_type_or_ref(fobj, &FILTERX_TYPE_NAME(dict_object)));
  assert_object_json_equals(fobj, "{\"foo\":1}");
  filterx_object_unref(fobj);

  /* dict does not accept a list */
  fobj = _exec_dict_func(filterx_object_from_json("[1, 2]", -1, NULL));
  cr_assert(fobj == NULL);

  fobj = _exec_dict_func(filterx_string_new("[1, 2]", -1));
  cr_assert(fobj == NULL);

  fobj = _exec_dict_func(filterx_message_value_new("[1, 2]", -1, LM_VT_STRING));
  cr_assert(fobj == NULL);

  fobj = _exec_dict_func(filterx_message_value_new("[1, 2]", -1, LM_VT_JSON));
  cr_assert(fobj == NULL);

  fobj = _exec_dict_func(filterx_message_value_new("foo,bar", -1, LM_VT_LIST));
  cr_assert(fobj == NULL);
}

Test(filterx_dict, test_list_function)
{
  FilterXObject *fobj;

  fobj = _exec_list_func(NULL);
  cr_assert(filterx_object_is_type_or_ref(fobj, &FILTERX_TYPE_NAME(list_object)));
  assert_object_json_equals(fobj, "[]");
  filterx_object_unref(fobj);

  fobj = _exec_list_func(filterx_string_new("[1, 2]", -1));
  cr_assert(filterx_object_is_type_or_ref(fobj, &FILTERX_TYPE_NAME(list_object)));
  assert_object_json_equals(fobj, "[1,2]");
  filterx_object_unref(fobj);

  /* no need to handle MessageValue arguments, those will be unmarshalled at
   * evaluation before passing them to the list() function */
  fobj = _exec_list_func(filterx_message_value_new("[1, 2]", -1, LM_VT_JSON));
  cr_assert(fobj == NULL);

  fobj = _exec_list_func(filterx_message_value_new("foo,bar", -1, LM_VT_LIST));
  cr_assert(fobj == NULL);

  fobj = _exec_list_func(filterx_message_value_new("[1, 2]", -1, LM_VT_STRING));
  cr_assert(fobj == NULL);

  fobj = _exec_list_func(filterx_object_from_json("[1, 2]", -1, NULL));
  cr_assert(filterx_object_is_type_or_ref(fobj, &FILTERX_TYPE_NAME(list_object)));
  assert_object_json_equals(fobj, "[1,2]");
  filterx_object_unref(fobj);

  fobj = _exec_list_func(filterx_object_from_json("{\"foo\":\"bar\"}", -1, NULL));
  cr_assert(fobj == NULL);

  fobj = _exec_list_func(filterx_string_new("{\"foo\":\"bar\"}", -1));
  cr_assert(fobj == NULL);

  fobj = _exec_list_func(filterx_message_value_new("{\"foo\":\"bar\"}", -1, LM_VT_STRING));
  cr_assert(fobj == NULL);

  fobj = _exec_list_func(filterx_message_value_new("{\"foo\":\"bar\"}", -1, LM_VT_JSON));
  cr_assert(fobj == NULL);
}

Test(filterx_dict, filterx_dict_object_repr)
{
  FilterXObject *obj = filterx_object_from_json("{\"foo\": \"foovalue\"}", -1, NULL);
  GString *repr = scratch_buffers_alloc();
  g_string_assign(repr, "foo");
  cr_assert(filterx_object_repr(obj, repr));
  cr_assert_str_eq("{\"foo\":\"foovalue\"}", repr->str);
  cr_assert(filterx_object_repr_append(obj, repr));
  cr_assert_str_eq("{\"foo\":\"foovalue\"}{\"foo\":\"foovalue\"}", repr->str);
  filterx_object_unref(obj);
}

Test(filterx_dict, filterx_dict_array_repr)
{
  FilterXObject *obj = filterx_object_from_json("[\"foo\", \"bar\"]", -1, NULL);
  GString *repr = scratch_buffers_alloc();
  g_string_assign(repr, "foo");
  cr_assert(filterx_object_repr(obj, repr));
  cr_assert_str_eq(repr->str, "[\"foo\",\"bar\"]");
  cr_assert(filterx_object_repr_append(obj, repr));
  cr_assert_str_eq(repr->str, "[\"foo\",\"bar\"][\"foo\",\"bar\"]");
  filterx_object_unref(obj);
}

Test(filterx_dict, filterx_dict_object_cloning_double)
{
  FilterXObject *obj = filterx_object_from_json("{\"foo\": 3.14}", -1, NULL);
  FilterXObject *obj_clone = filterx_object_clone(obj);
  cr_assert_not_null(obj_clone);
  filterx_object_unref(obj_clone);
  filterx_object_unref(obj);
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

TestSuite(filterx_dict, .init = setup, .fini = teardown);
