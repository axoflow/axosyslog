/*
 * Copyright (c) 2026 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#include "filterx/object-tuple.h"
#include "filterx/object-list.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "filterx/object-message-value.h"
#include "filterx/filterx-eval.h"
#include "filterx/expr-function.h"
#include "filterx/json-repr.h"
#include "scratch-buffers.h"
#include "apphook.h"
#include "cfg.h"

#include <criterion/criterion.h>
#include "libtest/filterx-lib.h"

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
_exec_tuple_func(FilterXObject *arg)
{
  return _exec_func(filterx_tuple_new_from_args, arg);
}

Test(filterx_tuple, test_tuple_function)
{
  FilterXObject *fobj;

  fobj = _exec_tuple_func(NULL);
  cr_assert(filterx_object_is_type_or_ref(fobj, &FILTERX_TYPE_NAME(tuple)));
  assert_object_json_equals(fobj, "[]");
  filterx_object_unref(fobj);

  fobj = _exec_tuple_func(filterx_string_new("[1, 2]", -1));
  cr_assert(filterx_object_is_type_or_ref(fobj, &FILTERX_TYPE_NAME(tuple)));
  assert_object_json_equals(fobj, "[1,2]");
  filterx_object_unref(fobj);

  /* no need to handle MessageValue arguments, those will be unmarshalled at
   * evaluation before passing them to the list() function */
  fobj = _exec_tuple_func(filterx_message_value_new("[1, 2]", -1, LM_VT_JSON));
  cr_assert(fobj == NULL);

  fobj = _exec_tuple_func(filterx_object_from_json("[1, 2]", -1, NULL));
  cr_assert(filterx_object_is_type_or_ref(fobj, &FILTERX_TYPE_NAME(tuple)));
  assert_object_json_equals(fobj, "[1,2]");
  filterx_object_unref(fobj);

  fobj = _exec_tuple_func(filterx_object_from_json("{\"foo\":\"bar\"}", -1, NULL));
  cr_assert(fobj == NULL);

  fobj = _exec_tuple_func(filterx_string_new("{\"foo\":\"bar\"}", -1));
  cr_assert(fobj == NULL);
}

Test(filterx_tuple, test_tuple_dedup)
{
  FilterXEvalContext context;
  filterx_eval_begin_compile(&context, configuration);

  FilterXObject *tuple = _exec_tuple_func(filterx_string_new("[\"a\", \"b\", \"a\"]", -1));
  FilterXObject *orig_tuple = tuple;

  filterx_eval_freeze_object(&tuple);

  cr_assert_eq(tuple, orig_tuple);

  FilterXObject *val_0 = filterx_sequence_get_subscript(tuple, 0);
  FilterXObject *val_1 = filterx_sequence_get_subscript(tuple, 1);
  FilterXObject *val_2 = filterx_sequence_get_subscript(tuple, 2);

  cr_assert_eq(val_0, val_2);
  cr_assert_neq(val_0, val_1);

  filterx_object_unref(val_2); filterx_object_unref(val_1); filterx_object_unref(val_0);
  filterx_object_unref(tuple);
  filterx_eval_end_compile(&context);
}

Test(filterx_tuple, filterx_tuple_array_repr_one_element)
{
  FilterXObject *obj = _exec_tuple_func(filterx_string_new("[\"foo\"]", -1));
  GString *repr = scratch_buffers_alloc();
  g_string_assign(repr, "foo");
  cr_assert(filterx_object_repr(obj, repr));
  cr_assert_str_eq(repr->str, "(\"foo\",)");
  filterx_object_unref(obj);
}

Test(filterx_tuple, filterx_tuple_array_repr_append)
{
  FilterXObject *obj = _exec_tuple_func(filterx_string_new("[\"foo\", \"bar\"]", -1));
  GString *repr = scratch_buffers_alloc();
  g_string_assign(repr, "foo");
  cr_assert(filterx_object_repr(obj, repr));
  cr_assert_str_eq(repr->str, "(\"foo\",\"bar\")");
  cr_assert(filterx_object_repr_append(obj, repr));
  cr_assert_str_eq(repr->str, "(\"foo\",\"bar\")(\"foo\",\"bar\")");
  filterx_object_unref(obj);
}

Test(filterx_tuple, test_tuple_function_from_tuple)
{
  FilterXObject *t1 = _exec_tuple_func(filterx_string_new("[1, 2]", -1));
  cr_assert_not_null(t1);

  FilterXObject *t2 = _exec_tuple_func(filterx_object_ref(t1));
  cr_assert_eq(t1, t2);

  filterx_object_unref(t2);
  filterx_object_unref(t1);
}

Test(filterx_tuple, test_tuple_repr_empty)
{
  FilterXObject *obj = filterx_tuple_new(0);
  GString *repr = scratch_buffers_alloc();
  cr_assert(filterx_object_repr(obj, repr));
  cr_assert_str_eq(repr->str, "()");
  filterx_object_unref(obj);
}

Test(filterx_tuple, test_tuple_marshal)
{
  FilterXObject *obj = _exec_tuple_func(filterx_string_new("[\"a\", \"b\"]", -1));
  assert_marshaled_object(obj, "[\"a\",\"b\"]", LM_VT_JSON);
  filterx_object_unref(obj);
}

Test(filterx_tuple, test_tuple_len)
{
  guint64 len;

  FilterXObject *obj = filterx_tuple_new(0);
  cr_assert(filterx_object_len(obj, &len));
  cr_assert_eq(len, 0);
  filterx_object_unref(obj);

  obj = _exec_tuple_func(filterx_string_new("[1, 2, 3]", -1));
  cr_assert(filterx_object_len(obj, &len));
  cr_assert_eq(len, 3);
  filterx_object_unref(obj);
}

Test(filterx_tuple, test_tuple_subscript)
{
  FilterXObject *obj = _exec_tuple_func(filterx_string_new("[1, 2, 3]", -1));

  FilterXObject *key = filterx_integer_new(0);
  FilterXObject *val = filterx_object_get_subscript(obj, key);
  cr_assert_not_null(val);
  assert_object_json_equals(val, "1");
  filterx_object_unref(val);
  filterx_object_unref(key);

  key = filterx_integer_new(2);
  val = filterx_object_get_subscript(obj, key);
  cr_assert_not_null(val);
  assert_object_json_equals(val, "3");
  filterx_object_unref(val);
  filterx_object_unref(key);

  key = filterx_integer_new(-1);
  val = filterx_object_get_subscript(obj, key);
  cr_assert_not_null(val);
  assert_object_json_equals(val, "3");
  filterx_object_unref(val);
  filterx_object_unref(key);

  key = filterx_integer_new(-3);
  val = filterx_object_get_subscript(obj, key);
  cr_assert_not_null(val);
  assert_object_json_equals(val, "1");
  filterx_object_unref(val);
  filterx_object_unref(key);

  key = filterx_integer_new(10);
  val = filterx_object_get_subscript(obj, key);
  cr_assert_null(val);
  filterx_object_unref(key);

  filterx_object_unref(obj);
}

Test(filterx_tuple, test_tuple_is_key_set)
{
  FilterXObject *obj = _exec_tuple_func(filterx_string_new("[1, 2, 3]", -1));

  FilterXObject *key = filterx_integer_new(0);
  cr_assert(filterx_object_is_key_set(obj, key));
  filterx_object_unref(key);

  key = filterx_integer_new(-1);
  cr_assert(filterx_object_is_key_set(obj, key));
  filterx_object_unref(key);

  key = filterx_integer_new(10);
  cr_assert_not(filterx_object_is_key_set(obj, key));
  filterx_object_unref(key);

  filterx_object_unref(obj);
}

Test(filterx_tuple, test_tuple_equal)
{
  FilterXObject *t1 = _exec_tuple_func(filterx_string_new("[1, 2, 3]", -1));
  FilterXObject *t2 = _exec_tuple_func(filterx_string_new("[1, 2, 3]", -1));
  FilterXObject *t3 = _exec_tuple_func(filterx_string_new("[1, 2]", -1));
  FilterXObject *t4 = _exec_tuple_func(filterx_string_new("[1, 2, 4]", -1));

  cr_assert(filterx_object_equal(t1, t2));
  cr_assert_not(filterx_object_equal(t1, t3));
  cr_assert_not(filterx_object_equal(t1, t4));

  filterx_object_unref(t4);
  filterx_object_unref(t3);
  filterx_object_unref(t2);
  filterx_object_unref(t1);
}

Test(filterx_tuple, test_tuple_hash)
{
  FilterXObject *t1 = _exec_tuple_func(filterx_string_new("[1, 2, 3]", -1));
  FilterXObject *t2 = _exec_tuple_func(filterx_string_new("[1, 2, 3]", -1));
  FilterXObject *t3 = _exec_tuple_func(filterx_string_new("[1, 2]", -1));

  cr_assert_eq(filterx_object_hash(t1), filterx_object_hash(t2));
  cr_assert_neq(filterx_object_hash(t1), filterx_object_hash(t3));

  filterx_object_unref(t3);
  filterx_object_unref(t2);
  filterx_object_unref(t1);
}

Test(filterx_tuple, test_tuple_clone)
{
  FilterXObject *orig = _exec_tuple_func(filterx_string_new("[1, 2, 3]", -1));
  FilterXObject *clone = filterx_object_copy(orig);

  cr_assert_not_null(clone);
  cr_assert_eq(orig, clone);
  assert_object_json_equals(clone, "[1,2,3]");

  guint64 len;
  cr_assert(filterx_object_len(clone, &len));
  cr_assert_eq(len, 3);

  filterx_object_unref(clone);
  filterx_object_unref(orig);
}

typedef struct
{
  gint count;
  FilterXObject *keys[8];
  FilterXObject *values[8];
} TupleIterCtx;

static gboolean
_collect_iter(FilterXObject *key, FilterXObject *value, gpointer user_data)
{
  TupleIterCtx *ctx = (TupleIterCtx *) user_data;
  ctx->keys[ctx->count] = filterx_object_ref(key);
  ctx->values[ctx->count] = filterx_object_ref(value);
  ctx->count++;
  return TRUE;
}

Test(filterx_tuple, test_tuple_iter)
{
  FilterXObject *obj = _exec_tuple_func(filterx_string_new("[\"a\", \"b\", \"c\"]", -1));
  TupleIterCtx ctx = {0};

  cr_assert(filterx_object_iter(obj, _collect_iter, &ctx));
  cr_assert_eq(ctx.count, 3);

  assert_object_json_equals(ctx.values[0], "\"a\"");
  assert_object_json_equals(ctx.values[1], "\"b\"");
  assert_object_json_equals(ctx.values[2], "\"c\"");

  for (gint i = 0; i < ctx.count; i++)
    {
      filterx_object_unref(ctx.keys[i]);
      filterx_object_unref(ctx.values[i]);
    }
  filterx_object_unref(obj);
}

static void
setup(void)
{
  app_startup();
  init_libtest_filterx();
  configuration = cfg_new_snippet();
}

static void
teardown(void)
{
  cfg_free(configuration);
  scratch_buffers_explicit_gc();
  deinit_libtest_filterx();
  app_shutdown();
}

TestSuite(filterx_tuple, .init = setup, .fini = teardown);
