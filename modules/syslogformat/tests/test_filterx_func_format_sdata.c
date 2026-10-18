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

#include "filterx-func-format-sdata.h"
#include "filterx/expr-literal.h"
#include "filterx/object-string.h"
#include "filterx/json-repr.h"

#include "apphook.h"
#include "scratch-buffers.h"

static FilterXObject *
_eval_format_sdata(const gchar *sdata_json)
{
  GError *err = NULL;
  GError *args_err = NULL;
  FilterXExpr *sdata = filterx_literal_new(filterx_object_from_json(sdata_json, -1, NULL));
  GList *args = g_list_append(NULL, filterx_function_arg_new(NULL, sdata));
  FilterXExpr *func = filterx_function_format_sdata_new(filterx_function_args_new(args, &args_err), &err);
  cr_assert(!err);

  FilterXObject *obj = init_and_eval_expr(func);
  filterx_expr_unref(func);
  return obj;
}

static void
_assert_format_sdata(const gchar *sdata_json, const gchar *expected_output)
{
  FilterXObject *obj = _eval_format_sdata(sdata_json);
  cr_assert(obj);
  cr_assert_str_eq(filterx_string_get_value_as_cstr(obj), expected_output);
  filterx_object_unref(obj);
}

Test(filterx_func_format_sdata, test_format)
{
  _assert_format_sdata("{\"foo\":{\"bar\":\"baz\",\"num\":42},\"empty\":{}}",
                       "[foo bar=\"baz\" num=\"42\"][empty]");
  _assert_format_sdata("{}", "-");
}

Test(filterx_func_format_sdata, test_escaping)
{
  _assert_format_sdata("{\"a b=\":{\"p]\":\"q\\\"\\\\]\"}}",
                       "[a%20b%3D p%5D=\"q\\\"\\\\\\]\"]");
}

Test(filterx_func_format_sdata, test_invalid_input)
{
  cr_assert_null(_eval_format_sdata("\"not a dict\""));
  cr_assert_null(_eval_format_sdata("{\"foo\":\"bar\"}"));
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

TestSuite(filterx_func_format_sdata, .init = setup, .fini = teardown);
