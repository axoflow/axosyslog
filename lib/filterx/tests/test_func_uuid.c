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

static FilterXExpr *
_create_uuid_expr(void)
{
  GError *error = NULL;
  FilterXExpr *fn = filterx_simple_function_new("uuid", filterx_function_args_new(NULL, NULL),
                                                filterx_simple_function_uuid, &error);
  cr_assert_null(error);
  return fn;
}

Test(filterx_func_uuid, returns_string)
{
  FilterXExpr *fn = _create_uuid_expr();
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_not_null(res);
  cr_assert(filterx_object_is_type(res, &FILTERX_TYPE_NAME(string)));

  gsize len;
  filterx_string_get_value_ref(res, &len);
  cr_assert_eq(len, 36);

  filterx_object_unref(res);
  filterx_expr_unref(fn);
}

Test(filterx_func_uuid, format_looks_like_uuid)
{
  FilterXExpr *fn = _create_uuid_expr();
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_not_null(res);

  gsize len;
  const gchar *uuid = filterx_string_get_value_ref(res, &len);
  cr_assert_not_null(uuid);

  /* xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx */
  cr_assert_eq(len, 36);
  cr_assert_eq(uuid[8], '-');
  cr_assert_eq(uuid[13], '-');
  cr_assert_eq(uuid[18], '-');
  cr_assert_eq(uuid[23], '-');

  filterx_object_unref(res);
  filterx_expr_unref(fn);
}

Test(filterx_func_uuid, two_calls_produce_different_uuids)
{
  FilterXExpr *fn1 = _create_uuid_expr();
  FilterXObject *res1 = init_and_eval_expr(fn1);
  cr_assert_not_null(res1);

  FilterXExpr *fn2 = _create_uuid_expr();
  FilterXObject *res2 = init_and_eval_expr(fn2);
  cr_assert_not_null(res2);

  gsize len1, len2;
  const gchar *uuid1 = filterx_string_get_value_ref(res1, &len1);
  const gchar *uuid2 = filterx_string_get_value_ref(res2, &len2);

  cr_assert(memcmp(uuid1, uuid2, 36) != 0);

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
                                                filterx_simple_function_uuid, &error);
  cr_assert_null(error);

  FilterXObject *res = init_and_eval_expr(fn);
  cr_assert_null(res);

  filterx_expr_unref(fn);
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
