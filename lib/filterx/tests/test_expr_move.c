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

#include "filterx/expr-variable.h"
#include "filterx/expr-compound.h"
#include "filterx/filterx-expr.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-scope.h"
#include "filterx/filterx-scope-var-layout.h"
#include "apphook.h"
#include "scratch-buffers.h"

Test(filterx_expr_move, moving_a_macro_fails_without_leaking_its_value)
{
  FilterXExpr *var = filterx_msg_variable_expr_new("$SEVERITY");
  FilterXExpr *block = filterx_compound_expr_new_va(FALSE, filterx_expr_ref(var), NULL);
  FilterXScopeVariableLayout *layout = filterx_scope_variable_layout_new(block);
  set_libtest_filterx_scope(filterx_scope_new(NULL, layout));
  cr_assert(filterx_expr_init(block, configuration));

  cr_assert_null(filterx_expr_move(var));
  filterx_eval_clear_errors();

  filterx_expr_deinit(block, configuration);
  filterx_expr_unref(block);
  filterx_expr_unref(var);
  filterx_scope_variable_layout_free(layout);
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

TestSuite(filterx_expr_move, .init = setup, .fini = teardown);
