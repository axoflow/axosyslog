/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Attila Szakacs-Bertok <attila.szakacs@axoflow.com>
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

#include "filterx/expr-membership.h"
#include "filterx/expr-literal.h"
#include "filterx/filterx-expr.h"
#include "filterx/filterx-eval.h"
#include "filterx/object-primitive.h"
#include "apphook.h"
#include "scratch-buffers.h"

Test(filterx_expr_membership, in_over_a_non_container_is_not_folded_and_fails_at_runtime)
{
  FilterXExpr *expr = filterx_membership_in_new(filterx_literal_new(filterx_integer_new(1)),
                                                filterx_literal_new(filterx_integer_new(2)));
  expr = filterx_expr_optimize(expr);
  cr_assert_not_null(expr);

  FilterXObject *result = init_and_eval_expr(expr);
  cr_assert_null(result);

  filterx_eval_clear_errors();
  filterx_expr_unref(expr);
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

TestSuite(filterx_expr_membership, .init = setup, .fini = teardown);
