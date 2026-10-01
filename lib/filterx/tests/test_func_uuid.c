/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Attila Szakacs <attila.szakacs@axoflow.com>
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
#include <criterion/new/assert.h>
#include "libtest/filterx-lib.h"

#include "filterx/func-uuid.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "filterx/expr-function.h"
#include "filterx/expr-literal.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-private.h"

#include "apphook.h"
#include "scratch-buffers.h"
#include "timeutils/cache.h"

static FilterXExpr *
_create_uuid_expr(void)
{
  GError *error = NULL;
  FilterXExpr *fn = filterx_simple_function_new("uuid", filterx_function_args_new(NULL, NULL),
                                                filterx_simple_function_uuid4, &error);
  cr_assert(zero(ptr, error));
  return fn;
}

Test(filterx_func_uuid, returns_string)
{
  FilterXExpr *fn = _create_uuid_expr();
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert(not(zero(ptr, res)));
  cr_assert(filterx_object_is_type(res, &FILTERX_TYPE_NAME(string)));

  gsize len;
  filterx_string_get_value_ref(res, &len);
  cr_assert(eq(sz, len, 36));

  filterx_object_unref(res);
  filterx_expr_unref(fn);
}

Test(filterx_func_uuid, format_looks_like_uuid)
{
  FilterXExpr *fn = _create_uuid_expr();
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert(not(zero(ptr, res)));

  gsize len;
  const gchar *uuid = filterx_string_get_value_ref(res, &len);
  cr_assert(not(zero(ptr, uuid)));

  /* xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx */
  cr_assert(eq(sz, len, 36));
  cr_assert(eq(chr, uuid[8], '-'));
  cr_assert(eq(chr, uuid[13], '-'));
  cr_assert(eq(chr, uuid[18], '-'));
  cr_assert(eq(chr, uuid[23], '-'));

  filterx_object_unref(res);
  filterx_expr_unref(fn);
}

Test(filterx_func_uuid, two_calls_produce_different_uuids)
{
  FilterXExpr *fn1 = _create_uuid_expr();
  FilterXObject *res1 = init_and_eval_expr(fn1);
  cr_assert(not(zero(ptr, res1)));

  FilterXExpr *fn2 = _create_uuid_expr();
  FilterXObject *res2 = init_and_eval_expr(fn2);
  cr_assert(not(zero(ptr, res2)));

  gsize len1, len2;
  const gchar *uuid1 = filterx_string_get_value_ref(res1, &len1);
  const gchar *uuid2 = filterx_string_get_value_ref(res2, &len2);

  cr_assert(ne(mem, ((struct cr_mem){ .data = uuid1, .size = 36 }), ((struct cr_mem){ .data = uuid2, .size = 36 })));

  filterx_object_unref(res1);
  filterx_expr_unref(fn1);
  filterx_object_unref(res2);
  filterx_expr_unref(fn2);
}

Test(filterx_func_uuid, rejects_arguments)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_object_expr_new(filterx_string_new("foo", -1))));

  GError *error = NULL;
  FilterXExpr *fn = filterx_simple_function_new("uuid", filterx_function_args_new(args, NULL),
                                                filterx_simple_function_uuid4, &error);
  cr_assert(zero(ptr, error));

  FilterXObject *res = init_and_eval_expr(fn);
  cr_assert(zero(ptr, res));

  filterx_expr_unref(fn);
}

static FilterXExpr *
_create_uuid7_expr(void)
{
  GError *error = NULL;
  FilterXExpr *fn = filterx_simple_function_new("uuid7", filterx_function_args_new(NULL, NULL),
                                                filterx_simple_function_uuid7, &error);
  cr_assert(zero(ptr, error));
  return fn;
}

Test(filterx_func_uuid, uuid7_format_version_and_variant)
{
  FilterXExpr *fn = _create_uuid7_expr();
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert(not(zero(ptr, res)));
  cr_assert(filterx_object_is_type(res, &FILTERX_TYPE_NAME(string)));

  gsize len;
  const gchar *uuid = filterx_string_get_value_ref(res, &len);
  cr_assert(not(zero(ptr, uuid)));

  cr_assert(eq(sz, len, 36));
  cr_assert(eq(chr, uuid[8], '-'));
  cr_assert(eq(chr, uuid[13], '-'));
  cr_assert(eq(chr, uuid[18], '-'));
  cr_assert(eq(chr, uuid[23], '-'));
  cr_assert(eq(chr, uuid[14], '7'));
  cr_assert(any(eq(chr, uuid[19], '8'), eq(chr, uuid[19], '9'), eq(chr, uuid[19], 'a'), eq(chr, uuid[19], 'b')));

  filterx_object_unref(res);
  filterx_expr_unref(fn);
}

Test(filterx_func_uuid, uuid7_embeds_current_timestamp)
{
  invalidate_cached_realtime();
  gint64 before_ms = g_get_real_time() / 1000;

  FilterXExpr *fn = _create_uuid7_expr();
  FilterXObject *res = init_and_eval_expr(fn);
  cr_assert(not(zero(ptr, res)));

  gint64 after_ms = g_get_real_time() / 1000;

  gsize len;
  const gchar *uuid = filterx_string_get_value_ref(res, &len);

  gchar ts_hex[13];
  memcpy(ts_hex, uuid, 8);
  memcpy(ts_hex + 8, uuid + 9, 4);
  ts_hex[12] = '\0';
  gint64 ts_ms = g_ascii_strtoll(ts_hex, NULL, 16);

  cr_assert(ge(i64, ts_ms, before_ms));
  cr_assert(le(i64, ts_ms, after_ms));

  filterx_object_unref(res);
  filterx_expr_unref(fn);
}

Test(filterx_func_uuid, uuid7_burst_is_strictly_monotonic)
{
  gchar prev[37] = "";

  for (gint i = 0; i < 64; i++)
    {
      FilterXExpr *fn = _create_uuid7_expr();
      FilterXObject *res = init_and_eval_expr(fn);
      cr_assert(not(zero(ptr, res)));

      gsize len;
      const gchar *uuid = filterx_string_get_value_ref(res, &len);
      cr_assert(eq(sz, len, 36));
      cr_assert(lt(int, strcmp(prev, uuid), 0));

      memcpy(prev, uuid, 36);
      prev[36] = '\0';

      filterx_object_unref(res);
      filterx_expr_unref(fn);
    }
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

TestSuite(filterx_func_uuid, .init = setup, .fini = teardown);
