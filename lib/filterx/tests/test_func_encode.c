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

#include "filterx/func-encode.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "filterx/expr-function.h"
#include "filterx/expr-literal.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-private.h"

#include "apphook.h"
#include "scratch-buffers.h"

/* base64 */

static FilterXExpr *
_create_base64_encode_expr(FilterXObject *arg)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_non_literal_new(arg)));

  GError *error = NULL;
  FilterXExpr *fn = filterx_simple_function_new("base64_encode", filterx_function_args_new(args, NULL),
                                                filterx_simple_function_base64_encode, &error);
  cr_assert_null(error);
  return fn;
}

static FilterXExpr *
_create_base64_decode_expr(FilterXObject *arg)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_non_literal_new(arg)));

  GError *error = NULL;
  FilterXExpr *fn = filterx_simple_function_new("base64_decode", filterx_function_args_new(args, NULL),
                                                filterx_simple_function_base64_decode, &error);
  cr_assert_null(error);
  return fn;
}

Test(filterx_func_base64, encode_string)
{
  FilterXExpr *fn = _create_base64_encode_expr(filterx_string_new("foobar", -1));
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_not_null(res);
  assert_object_str_equals(res, "Zm9vYmFy");

  filterx_object_unref(res);
  filterx_expr_unref(fn);
}

Test(filterx_func_base64, encode_bytes)
{
  FilterXExpr *fn = _create_base64_encode_expr(filterx_bytes_new("\x00\x01\x02\x03", 4));
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_not_null(res);
  assert_object_str_equals(res, "AAECAw==");

  filterx_object_unref(res);
  filterx_expr_unref(fn);
}

Test(filterx_func_base64, encode_wrong_arg_type)
{
  FilterXExpr *fn = _create_base64_encode_expr(filterx_integer_new(42));
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_null(res);

  filterx_expr_unref(fn);
}

Test(filterx_func_base64, decode_string)
{
  FilterXExpr *fn = _create_base64_decode_expr(filterx_string_new("Zm9vYmFy", -1));
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_not_null(res);
  cr_assert(filterx_object_is_type(res, &FILTERX_TYPE_NAME(bytes)));

  gsize len;
  const gchar *value = filterx_bytes_get_value_ref(res, &len);
  cr_assert_not_null(value);
  cr_assert_eq(len, 6);
  cr_assert(memcmp(value, "foobar", 6) == 0);

  filterx_object_unref(res);
  filterx_expr_unref(fn);
}

Test(filterx_func_base64, decode_wrong_arg_type)
{
  FilterXExpr *fn = _create_base64_decode_expr(filterx_integer_new(42));
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_null(res);

  filterx_expr_unref(fn);
}

Test(filterx_func_base64, encode_decode_roundtrip)
{
  const gchar *original = "szilvafa";
  FilterXExpr *encode_fn = _create_base64_encode_expr(filterx_string_new(original, -1));
  FilterXObject *encoded = init_and_eval_expr(encode_fn);
  cr_assert_not_null(encoded);

  FilterXExpr *decode_fn = _create_base64_decode_expr(filterx_object_ref(encoded));
  FilterXObject *decoded = init_and_eval_expr(decode_fn);
  cr_assert_not_null(decoded);

  gsize len;
  const gchar *value = filterx_bytes_get_value_ref(decoded, &len);
  cr_assert_not_null(value);
  cr_assert_eq(len, strlen(original));
  cr_assert(memcmp(value, original, len) == 0);

  filterx_object_unref(decoded);
  filterx_expr_unref(decode_fn);
  filterx_object_unref(encoded);
  filterx_expr_unref(encode_fn);
}

/* urlencode */

static FilterXExpr *
_create_urlencode_expr(FilterXObject *arg)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_non_literal_new(arg)));

  GError *error = NULL;
  FilterXExpr *fn = filterx_simple_function_new("urlencode", filterx_function_args_new(args, NULL),
                                                filterx_simple_function_urlencode, &error);
  cr_assert_null(error);
  return fn;
}

static FilterXExpr *
_create_urldecode_expr(FilterXObject *arg)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_non_literal_new(arg)));

  GError *error = NULL;
  FilterXExpr *fn = filterx_simple_function_new("urldecode", filterx_function_args_new(args, NULL),
                                                filterx_simple_function_urldecode, &error);
  cr_assert_null(error);
  return fn;
}

Test(filterx_func_url, encode_plain_string)
{
  FilterXExpr *fn = _create_urlencode_expr(filterx_string_new("foobar", -1));
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_not_null(res);
  assert_object_str_equals(res, "foobar");

  filterx_object_unref(res);
  filterx_expr_unref(fn);
}

Test(filterx_func_url, encode_special_chars)
{
  FilterXExpr *fn = _create_urlencode_expr(filterx_string_new("korte fa/szilva?alma=1&korte=2", -1));
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_not_null(res);
  assert_object_str_equals(res, "korte%20fa%2Fszilva%3Falma%3D1%26korte%3D2");

  filterx_object_unref(res);
  filterx_expr_unref(fn);
}

Test(filterx_func_url, encode_wrong_arg_type)
{
  FilterXExpr *fn = _create_urlencode_expr(filterx_integer_new(42));
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_null(res);

  filterx_expr_unref(fn);
}

Test(filterx_func_url, decode_plain_string)
{
  FilterXExpr *fn = _create_urldecode_expr(filterx_string_new("foobar", -1));
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_not_null(res);
  assert_object_str_equals(res, "foobar");

  filterx_object_unref(res);
  filterx_expr_unref(fn);
}

Test(filterx_func_url, decode_percent_encoded)
{
  FilterXExpr *fn = _create_urldecode_expr(filterx_string_new("korte%20fa%2Fszilva%3Falma%3D1%26korte%3D2", -1));
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_not_null(res);
  assert_object_str_equals(res, "korte fa/szilva?alma=1&korte=2");

  filterx_object_unref(res);
  filterx_expr_unref(fn);
}

Test(filterx_func_url, decode_wrong_arg_type)
{
  FilterXExpr *fn = _create_urldecode_expr(filterx_integer_new(42));
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_null(res);

  filterx_expr_unref(fn);
}

Test(filterx_func_url, encode_decode_roundtrip)
{
  const gchar *original = "kortefa/szilvafa?alma=1&dio=2";
  FilterXExpr *encode_fn = _create_urlencode_expr(filterx_string_new(original, -1));
  FilterXObject *encoded = init_and_eval_expr(encode_fn);
  cr_assert_not_null(encoded);

  FilterXExpr *decode_fn = _create_urldecode_expr(filterx_object_ref(encoded));
  FilterXObject *decoded = init_and_eval_expr(decode_fn);
  cr_assert_not_null(decoded);

  assert_object_str_equals(decoded, original);

  filterx_object_unref(decoded);
  filterx_expr_unref(decode_fn);
  filterx_object_unref(encoded);
  filterx_expr_unref(encode_fn);
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

/* hex */

static FilterXExpr *
_create_hex_encode_expr(FilterXObject *arg)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_non_literal_new(arg)));

  GError *error = NULL;
  FilterXExpr *fn = filterx_simple_function_new("hex_encode", filterx_function_args_new(args, NULL),
                                                filterx_simple_function_hex_encode, &error);
  cr_assert_null(error);
  return fn;
}

static FilterXExpr *
_create_hex_decode_expr(FilterXObject *arg)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_non_literal_new(arg)));

  GError *error = NULL;
  FilterXExpr *fn = filterx_simple_function_new("hex_decode", filterx_function_args_new(args, NULL),
                                                filterx_simple_function_hex_decode, &error);
  cr_assert_null(error);
  return fn;
}

Test(filterx_func_hex, encode_string)
{
  FilterXExpr *fn = _create_hex_encode_expr(filterx_string_new("foo", -1));
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_not_null(res);
  assert_object_str_equals(res, "666f6f");

  filterx_object_unref(res);
  filterx_expr_unref(fn);
}

Test(filterx_func_hex, encode_bytes)
{
  FilterXExpr *fn = _create_hex_encode_expr(filterx_bytes_new("\x00\x01\x0f\xff", 4));
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_not_null(res);
  assert_object_str_equals(res, "00010fff");

  filterx_object_unref(res);
  filterx_expr_unref(fn);
}

Test(filterx_func_hex, encode_wrong_arg_type)
{
  FilterXExpr *fn = _create_hex_encode_expr(filterx_integer_new(42));
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_null(res);

  filterx_expr_unref(fn);
}

Test(filterx_func_hex, decode_lowercase)
{
  FilterXExpr *fn = _create_hex_decode_expr(filterx_string_new("666f6f", -1));
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_not_null(res);
  cr_assert(filterx_object_is_type(res, &FILTERX_TYPE_NAME(bytes)));

  gsize len;
  const gchar *value = filterx_bytes_get_value_ref(res, &len);
  cr_assert_not_null(value);
  cr_assert_eq(len, 3);
  cr_assert(memcmp(value, "foo", 3) == 0);

  filterx_object_unref(res);
  filterx_expr_unref(fn);
}

Test(filterx_func_hex, decode_uppercase)
{
  FilterXExpr *fn = _create_hex_decode_expr(filterx_string_new("666F6F", -1));
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_not_null(res);
  cr_assert(filterx_object_is_type(res, &FILTERX_TYPE_NAME(bytes)));

  gsize len;
  const gchar *value = filterx_bytes_get_value_ref(res, &len);
  cr_assert_not_null(value);
  cr_assert_eq(len, 3);
  cr_assert(memcmp(value, "foo", 3) == 0);

  filterx_object_unref(res);
  filterx_expr_unref(fn);
}

Test(filterx_func_hex, decode_odd_length)
{
  FilterXExpr *fn = _create_hex_decode_expr(filterx_string_new("666f6", -1));
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_null(res);

  filterx_expr_unref(fn);
}

Test(filterx_func_hex, decode_invalid_char)
{
  FilterXExpr *fn = _create_hex_decode_expr(filterx_string_new("66zz6f", -1));
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_null(res);

  filterx_expr_unref(fn);
}

Test(filterx_func_hex, decode_wrong_arg_type)
{
  FilterXExpr *fn = _create_hex_decode_expr(filterx_integer_new(42));
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_null(res);

  filterx_expr_unref(fn);
}

Test(filterx_func_hex, encode_decode_roundtrip)
{
  FilterXExpr *encode_fn = _create_hex_encode_expr(filterx_bytes_new("\xde\xad\xbe\xef", 4));
  FilterXObject *encoded = init_and_eval_expr(encode_fn);
  cr_assert_not_null(encoded);

  FilterXExpr *decode_fn = _create_hex_decode_expr(filterx_object_ref(encoded));
  FilterXObject *decoded = init_and_eval_expr(decode_fn);
  cr_assert_not_null(decoded);

  gsize len;
  const gchar *value = filterx_bytes_get_value_ref(decoded, &len);
  cr_assert_not_null(value);
  cr_assert_eq(len, 4);
  cr_assert(memcmp(value, "\xde\xad\xbe\xef", 4) == 0);

  filterx_object_unref(decoded);
  filterx_expr_unref(decode_fn);
  filterx_object_unref(encoded);
  filterx_expr_unref(encode_fn);
}

TestSuite(filterx_func_base64, .init = setup, .fini = teardown);
TestSuite(filterx_func_url, .init = setup, .fini = teardown);
TestSuite(filterx_func_hex, .init = setup, .fini = teardown);
