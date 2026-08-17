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

static FilterXExpr *
_assign_empty_dict(const gchar *var)
{
  return filterx_assign_new(filterx_floating_variable_expr_new(var), filterx_literal_dict_new(NULL));
}

static FilterXExpr *
_read_attr(const gchar *var, const gchar *key)
{
  return filterx_getattr_new(filterx_floating_variable_expr_new(var), filterx_string_new(key, -1));
}

/* ownership of @value is taken */
static FilterXExpr *
_write_attr(const gchar *var, const gchar *key, FilterXExpr *value)
{
  return filterx_setattr_new(filterx_floating_variable_expr_new(var), filterx_string_new(key, -1), value);
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

Test(filterx_type_inference, assign_propagates_double_type_to_subsequent_reads)
{
  FilterXExpr *assign_d = filterx_assign_new(
                            filterx_floating_variable_expr_new("d"),
                            filterx_literal_new(filterx_double_new(2.5)));
  FilterXExpr *read_d = filterx_floating_variable_expr_new("d");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_d, read_d, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_d->static_type, FILTERX_STATIC_TYPE_DOUBLE);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, integer_and_double_are_distinct_static_types)
{
  /* if (c) { n = 1; } else { n = 2.5; } n  ->  UNKNOWN */
  FilterXExpr *then_assign = filterx_assign_new(
                               filterx_floating_variable_expr_new("n"),
                               filterx_literal_new(filterx_integer_new(1)));
  FilterXExpr *else_assign = filterx_assign_new(
                               filterx_floating_variable_expr_new("n"),
                               filterx_literal_new(filterx_double_new(2.5)));

  FilterXExpr *iff = filterx_conditional_new(filterx_floating_variable_expr_new("c"));
  filterx_conditional_set_true_branch(iff, then_assign);
  filterx_conditional_set_false_branch(iff, else_assign);

  FilterXExpr *read_n = filterx_floating_variable_expr_new("n");
  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, iff, read_n, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_n->static_type, FILTERX_STATIC_TYPE_UNKNOWN);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, literal_boolean_is_boolean)
{
  FilterXExpr *lit = filterx_literal_new(filterx_boolean_new(TRUE));
  lit = _optimize_and_infer(lit);
  cr_assert_eq(lit->static_type, FILTERX_STATIC_TYPE_BOOLEAN);
  filterx_expr_unref(lit);
}

Test(filterx_type_inference, boolean_and_integer_are_distinct_static_types)
{
  /* if (c) { n = true; } else { n = 1; } n  ->  UNKNOWN */
  FilterXExpr *then_assign = filterx_assign_new(
                               filterx_floating_variable_expr_new("n"),
                               filterx_literal_new(filterx_boolean_new(TRUE)));
  FilterXExpr *else_assign = filterx_assign_new(
                               filterx_floating_variable_expr_new("n"),
                               filterx_literal_new(filterx_integer_new(1)));

  FilterXExpr *iff = filterx_conditional_new(filterx_floating_variable_expr_new("c"));
  filterx_conditional_set_true_branch(iff, then_assign);
  filterx_conditional_set_false_branch(iff, else_assign);

  FilterXExpr *read_n = filterx_floating_variable_expr_new("n");
  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, iff, read_n, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_n->static_type, FILTERX_STATIC_TYPE_UNKNOWN);
  filterx_expr_unref(block);
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

Test(filterx_type_inference, assign_propagates_rhs_type_to_subsequent_reads)
{
  FilterXExpr *assign_x = filterx_assign_new(
                            filterx_floating_variable_expr_new("x"),
                            filterx_literal_new(filterx_string_new("v", -1)));
  FilterXExpr *read_x = filterx_floating_variable_expr_new("x");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_x, read_x, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_x->static_type, FILTERX_STATIC_TYPE_STRING);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, reassignment_with_different_type_collapses_later_reads)
{
  FilterXExpr *assign_dict = _assign_empty_dict("y");
  FilterXExpr *assign_str = filterx_assign_new(
                              filterx_floating_variable_expr_new("y"),
                              filterx_literal_new(filterx_string_new("v", -1)));
  FilterXExpr *read_y = filterx_floating_variable_expr_new("y");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_dict, assign_str, read_y, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_y->static_type, FILTERX_STATIC_TYPE_STRING);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, if_branch_meet_keeps_agreement)
{
  /* if (cond) { z = "a"; } else { z = "b"; } z  ->  z is STRING after the if */
  FilterXExpr *cond = filterx_floating_variable_expr_new("c");
  FilterXExpr *then_assign = filterx_assign_new(
                               filterx_floating_variable_expr_new("z"),
                               filterx_literal_new(filterx_string_new("a", -1)));
  FilterXExpr *else_assign = filterx_assign_new(
                               filterx_floating_variable_expr_new("z"),
                               filterx_literal_new(filterx_string_new("b", -1)));

  FilterXExpr *iff = filterx_conditional_new(cond);
  filterx_conditional_set_true_branch(iff, then_assign);
  filterx_conditional_set_false_branch(iff, else_assign);

  FilterXExpr *read_z = filterx_floating_variable_expr_new("z");
  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, iff, read_z, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_z->static_type, FILTERX_STATIC_TYPE_STRING);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, if_branch_meet_drops_on_disagreement)
{
  /* if (cond) { w = "a"; } else { w = []; } w  ->  w is UNKNOWN after the if */
  FilterXExpr *cond = filterx_floating_variable_expr_new("c");
  FilterXExpr *then_assign = filterx_assign_new(
                               filterx_floating_variable_expr_new("w"),
                               filterx_literal_new(filterx_string_new("a", -1)));
  FilterXExpr *else_assign = filterx_assign_new(
                               filterx_floating_variable_expr_new("w"),
                               filterx_literal_list_new(NULL));

  FilterXExpr *iff = filterx_conditional_new(cond);
  filterx_conditional_set_true_branch(iff, then_assign);
  filterx_conditional_set_false_branch(iff, else_assign);

  FilterXExpr *read_w = filterx_floating_variable_expr_new("w");
  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, iff, read_w, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_w->static_type, FILTERX_STATIC_TYPE_UNKNOWN);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, if_one_sided_assign_collapses_to_unknown)
{
  /* u was unknown before; one branch sets STRING, other does nothing.
   * Meet of STRING with UNKNOWN -> UNKNOWN. */
  FilterXExpr *cond = filterx_floating_variable_expr_new("c");
  FilterXExpr *then_assign = filterx_assign_new(
                               filterx_floating_variable_expr_new("u"),
                               filterx_literal_new(filterx_string_new("a", -1)));

  FilterXExpr *iff = filterx_conditional_new(cond);
  filterx_conditional_set_true_branch(iff, then_assign);

  FilterXExpr *read_u = filterx_floating_variable_expr_new("u");
  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, iff, read_u, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_u->static_type, FILTERX_STATIC_TYPE_UNKNOWN);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, reassigning_root_invalidates_the_whole_range_under_it)
{
  /* d = {}; d.a = {};   -> d.a is a DICT
   * d = "s";            -> the variable is overwritten; d is now a STRING
   * d.a                 -> must NOT still resolve to DICT.  Writing the zero-step path drops
   *                        the range below it, so the entry describing the old value is gone. */
  FilterXExpr *assign_dict = _assign_empty_dict("d");
  FilterXExpr *set_a = _write_attr("d", "a", filterx_literal_dict_new(NULL));
  FilterXExpr *reassign_str = filterx_assign_new(
                                filterx_floating_variable_expr_new("d"),
                                filterx_literal_new(filterx_string_new("s", -1)));
  FilterXExpr *read_a = _read_attr("d", "a");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_dict, set_a, reassign_str, read_a, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_a->static_type, FILTERX_STATIC_TYPE_UNKNOWN);
  filterx_expr_unref(block);
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
