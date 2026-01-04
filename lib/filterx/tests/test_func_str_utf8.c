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

#include "filterx/func-str.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "filterx/expr-function.h"
#include "filterx/expr-literal.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-private.h"

#include "apphook.h"
#include "scratch-buffers.h"

/* utf8_validate */

static FilterXExpr *
_create_utf8_validate_expr(FilterXObject *arg)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_object_expr_new(arg)));

  GError *error = NULL;
  FilterXExpr *fn = filterx_simple_function_new("utf8_validate", filterx_function_args_new(args, NULL),
                                                filterx_simple_function_utf8_validate, &error);
  cr_assert_null(error);
  return fn;
}

Test(filterx_func_utf8_validate, valid_string)
{
  FilterXExpr *fn = _create_utf8_validate_expr(filterx_string_new("foobar", -1));
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_not_null(res);
  assert_object_repr_equals(res, "true");

  filterx_object_unref(res);
  filterx_expr_unref(fn);
}

Test(filterx_func_utf8_validate, valid_utf8_string)
{
  /* "almafá" in UTF-8: valid multibyte sequence */
  const gchar *almafa_utf8 = "almaf\xc3\xa1";
  FilterXExpr *fn = _create_utf8_validate_expr(filterx_string_new(almafa_utf8, -1));
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_not_null(res);
  assert_object_repr_equals(res, "true");

  filterx_object_unref(res);
  filterx_expr_unref(fn);
}

Test(filterx_func_utf8_validate, invalid_utf8_string)
{
  /* 0x80 is not a valid UTF-8 start byte */
  FilterXExpr *fn = _create_utf8_validate_expr(filterx_string_new("\x80\x81\x82", 3));
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_not_null(res);
  assert_object_repr_equals(res, "false");

  filterx_object_unref(res);
  filterx_expr_unref(fn);
}

Test(filterx_func_utf8_validate, wrong_arg_type)
{
  FilterXExpr *fn = _create_utf8_validate_expr(filterx_integer_new(42));
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_null(res);

  filterx_expr_unref(fn);
}

/* utf8_sanitize */

static FilterXExpr *
_create_utf8_sanitize_expr(FilterXObject *arg)
{
  GList *args = NULL;
  args = g_list_append(args, filterx_function_arg_new(NULL, filterx_object_expr_new(arg)));

  GError *error = NULL;
  FilterXExpr *fn = filterx_simple_function_new("utf8_sanitize", filterx_function_args_new(args, NULL),
                                                filterx_simple_function_utf8_sanitize, &error);
  cr_assert_null(error);
  return fn;
}

Test(filterx_func_utf8_sanitize, valid_string_unchanged)
{
  FilterXExpr *fn = _create_utf8_sanitize_expr(filterx_string_new("foobar", -1));
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_not_null(res);
  assert_object_repr_equals(res, "foobar");

  filterx_object_unref(res);
  filterx_expr_unref(fn);
}

Test(filterx_func_utf8_sanitize, invalid_utf8_string_escaped)
{
  /* 0x80, 0x81, 0x82 are invalid UTF-8 start bytes */
  FilterXExpr *fn = _create_utf8_sanitize_expr(filterx_string_new("\x80\x81\x82", 3));
  FilterXObject *res = init_and_eval_expr(fn);

  cr_assert_not_null(res);
  assert_object_repr_equals(res, "\\x80\\x81\\x82");

  filterx_object_unref(res);
  filterx_expr_unref(fn);
}

Test(filterx_func_utf8_sanitize, idempotent)
{
  /* Escaped output is valid UTF-8, so a second call returns it unchanged. */
  FilterXExpr *fn = _create_utf8_sanitize_expr(filterx_string_new("\x80\x81\x82", 3));
  FilterXObject *first = init_and_eval_expr(fn);
  cr_assert_not_null(first);
  filterx_expr_unref(fn);

  fn = _create_utf8_sanitize_expr(filterx_object_ref(first));
  FilterXObject *second = init_and_eval_expr(fn);
  cr_assert_not_null(second);

  assert_object_repr_equals(second, "\\x80\\x81\\x82");

  filterx_object_unref(second);
  filterx_expr_unref(fn);
  filterx_object_unref(first);
}

Test(filterx_func_utf8_sanitize, wrong_arg_type)
{
  FilterXExpr *fn = _create_utf8_sanitize_expr(filterx_integer_new(42));
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

TestSuite(filterx_func_utf8_validate, .init = setup, .fini = teardown);
TestSuite(filterx_func_utf8_sanitize, .init = setup, .fini = teardown);
