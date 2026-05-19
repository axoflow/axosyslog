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

#include "filterx/func-digest.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "filterx/expr-function.h"
#include "filterx/expr-literal.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-private.h"

#include "apphook.h"
#include "scratch-buffers.h"

static void
_assert_bytes_result(FilterXObject *res, const gchar *expected, gsize expected_len)
{
  cr_assert_not_null(res);
  gsize len;
  const gchar *bytes = filterx_bytes_get_value_ref(res, &len);
  cr_assert_not_null(bytes);
  cr_assert_eq(len, expected_len);
  cr_assert(memcmp(bytes, expected, expected_len) == 0);
}

static FilterXExpr *
_create_simple_digest_expr(FilterXObject *arg, FilterXSimpleFunctionProto fn, const gchar *fn_name)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_non_literal_new(arg)));

  GError *error = NULL;
  FilterXExpr *fn_expr = filterx_simple_function_new(fn_name, filterx_function_args_new(args, NULL), fn, &error);
  cr_assert_null(error);
  return fn_expr;
}

static FilterXExpr *
_create_digest_expr(FilterXObject *arg, const gchar *alg)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_non_literal_new(arg)));
  if (alg)
    args = g_list_append(args, filterx_function_arg_new("alg", filterx_literal_new(filterx_string_new(alg, -1))));

  GError *error = NULL;
  FilterXExpr *fn = filterx_function_digest_new(filterx_function_args_new(args, NULL), &error);
  cr_assert_null(error);
  return fn;
}

Test(filterx_func_digest, md5_string)
{
  FilterXExpr *fn = _create_simple_digest_expr(filterx_string_new("foo", -1),
                                               filterx_simple_function_md5, "md5");
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_not_null(res);
  assert_object_str_equals(res, "acbd18db4cc2f85cedef654fccc4a4d8");

  filterx_object_unref(res);
  filterx_expr_unref(fn);
}

Test(filterx_func_digest, sha1_string)
{
  FilterXExpr *fn = _create_simple_digest_expr(filterx_string_new("foo", -1),
                                               filterx_simple_function_sha1, "sha1");
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_not_null(res);
  assert_object_str_equals(res, "0beec7b5ea3f0fdbc95d0dd47f3c5bc275da8a33");

  filterx_object_unref(res);
  filterx_expr_unref(fn);
}

Test(filterx_func_digest, sha256_string)
{
  FilterXExpr *fn = _create_simple_digest_expr(filterx_string_new("foo", -1),
                                               filterx_simple_function_sha256, "sha256");
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_not_null(res);
  assert_object_str_equals(res, "2c26b46b68ffc68ff99b453c1d30413413422d706483bfa0f98a5e886266e7ae");

  filterx_object_unref(res);
  filterx_expr_unref(fn);
}

Test(filterx_func_digest, sha512_string)
{
  FilterXExpr *fn = _create_simple_digest_expr(filterx_string_new("foo", -1),
                                               filterx_simple_function_sha512, "sha512");
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_not_null(res);
  assert_object_str_equals(res,
                            "f7fbba6e0636f890e56fbbf3283e524c"
                            "6fa3204ae298382d624741d0dc663832"
                            "6e282c41be5e4254d8820772c5518a2c"
                            "5a8c0c7f7eda19594a7eb539453e1ed7");

  filterx_object_unref(res);
  filterx_expr_unref(fn);
}

Test(filterx_func_digest, digest_bytes_input)
{
  FilterXExpr *fn = _create_simple_digest_expr(filterx_bytes_new("\x00\x01\x02\x03", 4),
                                               filterx_simple_function_md5, "md5");
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_not_null(res);
  assert_object_str_equals(res, "37b59afd592725f9305e484a5d7f5168");

  filterx_object_unref(res);
  filterx_expr_unref(fn);
}

Test(filterx_func_digest, wrong_arg_type)
{
  FilterXExpr *fn = _create_simple_digest_expr(filterx_integer_new(42),
                                               filterx_simple_function_sha256, "sha256");
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_null(res);

  filterx_expr_unref(fn);
}

Test(filterx_func_digest, digest_default_alg)
{
  /* digest() defaults to sha256 */
  FilterXExpr *fn = _create_digest_expr(filterx_string_new("foo", -1), NULL);
  FilterXObject *res = init_and_eval_expr(fn);

  /* sha256("foo") = 2c26b46b68ffc68ff99b453c1d30413413422d706483bfa0f98a5e886266e7ae */
  _assert_bytes_result(res,
                       "\x2c\x26\xb4\x6b\x68\xff\xc6\x8f\xf9\x9b\x45\x3c\x1d\x30\x41\x34"
                       "\x13\x42\x2d\x70\x64\x83\xbf\xa0\xf9\x8a\x5e\x88\x62\x66\xe7\xae",
                       32);

  filterx_object_unref(res);
  filterx_expr_unref(fn);
}

Test(filterx_func_digest, digest_custom_alg)
{
  FilterXExpr *fn = _create_digest_expr(filterx_string_new("foo", -1), "md5");
  FilterXObject *res = init_and_eval_expr(fn);

  /* md5("foo") = acbd18db4cc2f85cedef654fccc4a4d8 */
  _assert_bytes_result(res,
                       "\xac\xbd\x18\xdb\x4c\xc2\xf8\x5c\xed\xef\x65\x4f\xcc\xc4\xa4\xd8",
                       16);

  filterx_object_unref(res);
  filterx_expr_unref(fn);
}

Test(filterx_func_digest, digest_unknown_alg)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL,
                                                      filterx_non_literal_new(filterx_string_new("foo", -1))));
  args = g_list_append(args, filterx_function_arg_new("alg",
                                                      filterx_literal_new(filterx_string_new("bogus_alg", -1))));

  GError *error = NULL;
  FilterXExpr *fn = filterx_function_digest_new(filterx_function_args_new(args, NULL), &error);

  cr_assert_null(fn);
  cr_assert_not_null(error);
  g_error_free(error);
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

TestSuite(filterx_func_digest, .init = setup, .fini = teardown);
