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

#include "filterx/filterx-type-inference.h"
#include "filterx/filterx-expr.h"
#include "filterx/expr-literal.h"
#include "filterx/expr-literal-container.h"
#include "filterx/expr-variable.h"
#include "filterx/expr-assign.h"
#include "filterx/expr-compound.h"
#include "filterx/expr-condition.h"
#include "filterx/expr-plus.h"
#include "filterx/expr-plus-assign.h"
#include "filterx/object-string.h"
#include "filterx/object-dict.h"
#include "filterx/object-list.h"
#include "filterx/expr-get-subscript.h"
#include "filterx/expr-getattr.h"
#include "filterx/expr-set-subscript.h"
#include "filterx/expr-setattr.h"

#include "apphook.h"
#include "scratch-buffers.h"
#include "libtest/filterx-lib.h"

static FilterXExpr *
_run(FilterXExpr *root)
{
  root = filterx_expr_optimize(root);
  filterx_expr_infer_types_root(root);
  return root;
}

Test(filterx_type_inference, literal_string_is_string)
{
  FilterXExpr *lit = filterx_literal_new(filterx_string_new("hi", -1));
  lit = _run(lit);
  cr_assert_eq(filterx_static_type_kind(lit->static_type), FILTERX_STATIC_TYPE_STRING);
  filterx_expr_unref(lit);
}

Test(filterx_type_inference, literal_dict_is_dict)
{
  FilterXExpr *empty_dict = filterx_literal_dict_new(NULL);
  empty_dict = _run(empty_dict);
  /* Fully-literal containers fold to a 'literal' expr at optimize time, and the
   * literal's inference reads the underlying FilterXObject type. An empty container has no
   * element, so the kind is DICT and element() is UNKNOWN. */
  cr_assert_eq(filterx_static_type_kind(empty_dict->static_type), FILTERX_STATIC_TYPE_DICT);
  cr_assert_eq(filterx_static_type_kind(filterx_static_type_element(empty_dict->static_type)),
               FILTERX_STATIC_TYPE_UNKNOWN);
  filterx_expr_unref(empty_dict);
}

Test(filterx_type_inference, literal_list_is_list)
{
  FilterXExpr *empty_list = filterx_literal_list_new(NULL);
  empty_list = _run(empty_list);
  cr_assert_eq(filterx_static_type_kind(empty_list->static_type), FILTERX_STATIC_TYPE_LIST);
  cr_assert_eq(filterx_static_type_kind(filterx_static_type_element(empty_list->static_type)),
               FILTERX_STATIC_TYPE_UNKNOWN);
  filterx_expr_unref(empty_list);
}

Test(filterx_type_inference, assign_propagates_rhs_type_to_subsequent_reads)
{
  FilterXExpr *assign_x = filterx_assign_new(
                            filterx_floating_variable_expr_new("x"),
                            filterx_literal_new(filterx_string_new("v", -1)));
  FilterXExpr *read_x = filterx_floating_variable_expr_new("x");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_x, read_x, NULL);
  block = _run(block);

  cr_assert_eq(filterx_static_type_kind(read_x->static_type), FILTERX_STATIC_TYPE_STRING);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, reassignment_with_different_type_collapses_later_reads)
{
  FilterXExpr *assign_dict = filterx_assign_new(
                               filterx_floating_variable_expr_new("y"),
                               filterx_literal_dict_new(NULL));
  FilterXExpr *assign_str = filterx_assign_new(
                              filterx_floating_variable_expr_new("y"),
                              filterx_literal_new(filterx_string_new("v", -1)));
  FilterXExpr *read_y = filterx_floating_variable_expr_new("y");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_dict, assign_str, read_y, NULL);
  block = _run(block);

  cr_assert_eq(filterx_static_type_kind(read_y->static_type), FILTERX_STATIC_TYPE_STRING);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, if_branch_meet_keeps_agreement)
{
  /* if (cond) { $z = "a"; } else { $z = "b"; } $z   ->  $z is STRING after the if. */
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
  block = _run(block);

  cr_assert_eq(filterx_static_type_kind(read_z->static_type), FILTERX_STATIC_TYPE_STRING);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, if_branch_meet_drops_on_disagreement)
{
  /* if (cond) { $w = "a"; } else { $w = []; } $w   ->  $w is UNKNOWN after the if. */
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
  block = _run(block);

  cr_assert_eq(filterx_static_type_kind(read_w->static_type), FILTERX_STATIC_TYPE_UNKNOWN);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, if_one_sided_assign_collapses_to_unknown)
{
  /* $u was unknown before; one branch sets STRING, other does nothing.
   * Meet of STRING with UNKNOWN -> UNKNOWN. */
  FilterXExpr *cond = filterx_floating_variable_expr_new("c");
  FilterXExpr *then_assign = filterx_assign_new(
                               filterx_floating_variable_expr_new("u"),
                               filterx_literal_new(filterx_string_new("a", -1)));

  FilterXExpr *iff = filterx_conditional_new(cond);
  filterx_conditional_set_true_branch(iff, then_assign);

  FilterXExpr *read_u = filterx_floating_variable_expr_new("u");
  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, iff, read_u, NULL);
  block = _run(block);

  cr_assert_eq(filterx_static_type_kind(read_u->static_type), FILTERX_STATIC_TYPE_UNKNOWN);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, plus_of_two_strings_is_string)
{
  FilterXExpr *p = filterx_operator_plus_new(
                     filterx_literal_new(filterx_string_new("a", -1)),
                     filterx_literal_new(filterx_string_new("b", -1)));
  p = _run(p);
  /* plus optimizes literal+literal to a literal; either way the static_type should resolve. */
  cr_assert_eq(filterx_static_type_kind(p->static_type), FILTERX_STATIC_TYPE_STRING);
  filterx_expr_unref(p);
}

Test(filterx_type_inference, folded_literal_dict_of_string_tracks_element_type)
{
  /* {"a": "x", "b": "y"} — fully literal, folds to a single FilterXDict literal. */
  GList *elems = NULL;
  elems = g_list_append(elems, filterx_literal_element_new(
                          filterx_literal_new(filterx_string_new("a", -1)),
                          filterx_literal_new(filterx_string_new("x", -1))));
  elems = g_list_append(elems, filterx_literal_element_new(
                          filterx_literal_new(filterx_string_new("b", -1)),
                          filterx_literal_new(filterx_string_new("y", -1))));
  FilterXExpr *d = filterx_literal_dict_new(elems);
  d = _run(d);

  cr_assert_eq(filterx_static_type_kind(d->static_type), FILTERX_STATIC_TYPE_DICT);
  cr_assert_eq(filterx_static_type_kind(filterx_static_type_element(d->static_type)),
               FILTERX_STATIC_TYPE_STRING);
  filterx_expr_unref(d);
}

Test(filterx_type_inference, mixed_value_types_in_literal_dict_collapse_element)
{
  /* {"a": "x", "b": [1, 2]} — string and list values; element type meets to UNKNOWN. */
  GList *list_elems = g_list_append(NULL, filterx_literal_element_new(
                                      NULL,
                                      filterx_literal_new(filterx_integer_new(1))));
  GList *elems = NULL;
  elems = g_list_append(elems, filterx_literal_element_new(
                          filterx_literal_new(filterx_string_new("a", -1)),
                          filterx_literal_new(filterx_string_new("x", -1))));
  elems = g_list_append(elems, filterx_literal_element_new(
                          filterx_literal_new(filterx_string_new("b", -1)),
                          filterx_literal_list_new(list_elems)));
  FilterXExpr *d = filterx_literal_dict_new(elems);
  d = _run(d);

  cr_assert_eq(filterx_static_type_kind(d->static_type), FILTERX_STATIC_TYPE_DICT);
  /* element type collapses because string and list don't meet. */
  cr_assert_eq(filterx_static_type_kind(filterx_static_type_element(d->static_type)), FILTERX_STATIC_TYPE_UNKNOWN);
  filterx_expr_unref(d);
}

Test(filterx_type_inference, get_subscript_shifts_operand_spec)
{
  /* d = {"a": "x", "b": "y"} → DICT_OF_STRING. d["a"] → STRING. */
  GList *elems = g_list_append(NULL, filterx_literal_element_new(
                                 filterx_literal_new(filterx_string_new("a", -1)),
                                 filterx_literal_new(filterx_string_new("x", -1))));
  FilterXExpr *assign = filterx_assign_new(
                          filterx_floating_variable_expr_new("d"),
                          filterx_literal_dict_new(elems));
  FilterXExpr *read = filterx_get_subscript_new(
                        filterx_floating_variable_expr_new("d"),
                        filterx_literal_new(filterx_string_new("a", -1)));
  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign, read, NULL);
  block = _run(block);

  cr_assert_eq(filterx_static_type_kind(read->static_type), FILTERX_STATIC_TYPE_STRING);
  filterx_expr_unref(block);
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

TestSuite(filterx_type_inference, .init = setup, .fini = teardown);
