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

#include "filterx/expr-arithmetic-operators.h"
#include "filterx/filterx-type-inference.h"
#include "filterx/filterx-scope.h"
#include "filterx/filterx-expr.h"
#include "filterx/expr-literal.h"
#include "filterx/expr-literal-container.h"
#include "filterx/expr-variable.h"
#include "filterx/expr-assign.h"
#include "filterx/expr-compound.h"
#include "filterx/expr-condition.h"
#include "filterx/expr-plus-assign.h"
#include "filterx/object-string.h"
#include "filterx/object-dict.h"
#include "filterx/object-list.h"
#include "filterx/expr-get-subscript.h"
#include "filterx/expr-getattr.h"
#include "filterx/expr-set-subscript.h"
#include "filterx/expr-setattr.h"
#include "filterx/expr-switch.h"
#include "filterx/expr-unset.h"
#include "filterx/expr-move.h"
#include "filterx/expr-function.h"
#include "filterx/expr-comparison.h"
#include "filterx/expr-boolalg.h"
#include "filterx/expr-isset.h"
#include "filterx/expr-membership.h"
#include "filterx/expr-regexp.h"
#include "filterx/expr-break.h"
#include "filterx/expr-drop.h"
#include "filterx/expr-done.h"
#include "filterx/filterx-dpath.h"
#include "filterx/func-unset-empties.h"
#include "filterx/filterx-expr.h"

#include "apphook.h"
#include "scratch-buffers.h"
#include "libtest/filterx-lib.h"

static FilterXExpr *
_optimize_and_infer(FilterXExpr *root)
{
  root = filterx_expr_optimize(root);

  FilterXTypeEnv *env = filterx_type_env_new();
  filterx_expr_infer_types(root, env);
  filterx_type_env_free(env);

  return root;
}

Test(filterx_type_inference, literal_string_is_string)
{
  FilterXExpr *lit = filterx_literal_new(filterx_string_new("hi", -1));
  lit = _optimize_and_infer(lit);
  cr_assert_eq(lit->static_type, FILTERX_STATIC_TYPE_STRING);
  filterx_expr_unref(lit);
}

Test(filterx_type_inference, literal_double_is_double)
{
  FilterXExpr *lit = filterx_literal_new(filterx_double_new(2.5));
  lit = _optimize_and_infer(lit);
  cr_assert_eq(lit->static_type, FILTERX_STATIC_TYPE_DOUBLE);
  filterx_expr_unref(lit);
}

Test(filterx_type_inference, literal_boolean_is_boolean)
{
  FilterXExpr *lit = filterx_literal_new(filterx_boolean_new(TRUE));
  lit = _optimize_and_infer(lit);
  cr_assert_eq(lit->static_type, FILTERX_STATIC_TYPE_BOOLEAN);
  filterx_expr_unref(lit);
}

Test(filterx_type_inference, literal_dict_is_dict)
{
  FilterXExpr *empty_dict = filterx_literal_dict_new(NULL);
  empty_dict = _optimize_and_infer(empty_dict);
  cr_assert_eq(empty_dict->static_type, FILTERX_STATIC_TYPE_DICT);
  filterx_expr_unref(empty_dict);
}

Test(filterx_type_inference, literal_list_is_list)
{
  FilterXExpr *empty_list = filterx_literal_list_new(NULL);
  empty_list = _optimize_and_infer(empty_list);
  cr_assert_eq(empty_list->static_type, FILTERX_STATIC_TYPE_LIST);
  filterx_expr_unref(empty_list);
}

Test(filterx_type_inference, macro_variable_is_always_unknown)
{
  /* a hard macro is computed straight from the message rather than through the scope, so the read
   * is UNKNOWN even though $FACILITY is always a string at runtime */
  FilterXExpr *read_facility = filterx_msg_variable_expr_new("FACILITY");
  cr_assert(filterx_variable_expr_is_macro(read_facility));

  read_facility = _optimize_and_infer(read_facility);
  cr_assert_eq(read_facility->static_type, FILTERX_STATIC_TYPE_UNKNOWN);
  filterx_expr_unref(read_facility);
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
  deinit_libtest_filterx();
  scratch_buffers_explicit_gc();
  app_shutdown();
}

/* Without the JIT every infer_types hook is compiled out, so there is nothing to assert against. */
#if SYSLOG_NG_ENABLE_JIT
#define TYPE_INFERENCE_TESTS_DISABLED FALSE
#else
#define TYPE_INFERENCE_TESTS_DISABLED TRUE
#endif

TestSuite(filterx_type_inference, .init = setup, .fini = teardown,
          .disabled = TYPE_INFERENCE_TESTS_DISABLED);
