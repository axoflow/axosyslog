/*
 * Copyright (c) 2026 Axoflow
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

#include "filterx/func-set-pri.h"
#include "filterx/object-primitive.h"
#include "filterx/expr-function.h"
#include "filterx/filterx-object.h"

#include "apphook.h"
#include "scratch-buffers.h"

Test(filterx_func_set_pri, null_argument_eval_does_not_crash)
{
  GList *args = g_list_append(NULL,
                              filterx_function_arg_new(NULL, filterx_dummy_error_new("argument failed to evaluate")));

  GError *err = NULL;
  GError *args_err = NULL;
  FilterXExpr *func = filterx_function_set_pri_new(filterx_function_args_new(args, &args_err), &err);
  cr_assert_null(err);
  cr_assert_not_null(func);

  FilterXObject *res = init_and_eval_expr(func);
  cr_assert_null(res);

  filterx_expr_unref(func);
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

TestSuite(filterx_func_set_pri, .init = setup, .fini = teardown);
