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
#include "filterx/filterx-type-inference-private.h"
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

static FilterXExpr *
_read_attr_of(FilterXExpr *object, const gchar *key)
{
  return filterx_getattr_new(object, filterx_string_new(key, -1));
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

Test(filterx_type_inference, boolean_is_not_numeric_for_the_plus_operator)
{
  /* a boolean has no add() method */
  FilterXExpr *sum = filterx_operator_plus_new(
                       filterx_literal_new(filterx_boolean_new(TRUE)),
                       filterx_literal_new(filterx_integer_new(1)));
  sum = _optimize_and_infer(sum);
  cr_assert_eq(sum->static_type, FILTERX_STATIC_TYPE_UNKNOWN);
  filterx_expr_unref(sum);
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

Test(filterx_type_inference, plus_of_two_strings_is_string)
{
  FilterXExpr *p = filterx_operator_plus_new(
                     filterx_literal_new(filterx_string_new("a", -1)),
                     filterx_literal_new(filterx_string_new("b", -1)));
  p = _optimize_and_infer(p);
  /* plus optimizes literal+literal to a literal; either way the static_type should resolve. */
  cr_assert_eq(p->static_type, FILTERX_STATIC_TYPE_STRING);
  filterx_expr_unref(p);
}

/* Build `<var> = <literal>;` — plus constant-folds a literal+literal pair, so operands have
 * to come from variables for _infer_plus_types to be the thing under test. */
static FilterXExpr *
_assign_literal(const gchar *name, FilterXObject *value)
{
  return filterx_assign_new(filterx_floating_variable_expr_new(name), filterx_literal_new(value));
}

Test(filterx_type_inference, plus_of_two_integers_is_integer)
{
  /* a = 1; b = 2; a + b  ->  INTEGER. */
  FilterXExpr *sum = filterx_operator_plus_new(filterx_floating_variable_expr_new("a"),
                                               filterx_floating_variable_expr_new("b"));
  FilterXExpr *block = filterx_compound_expr_new_va(TRUE,
                                                    _assign_literal("a", filterx_integer_new(1)),
                                                    _assign_literal("b", filterx_integer_new(2)),
                                                    sum, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(sum->static_type, FILTERX_STATIC_TYPE_INTEGER);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, plus_of_integer_and_double_is_double)
{
  /* a = 1; d = 2.5; a + d  ->  DOUBLE, in both operand orders: filterx_object_add() widens
   * to double whichever side carries it, so the result does not depend on the order. */
  FilterXExpr *int_first = filterx_operator_plus_new(filterx_floating_variable_expr_new("a"),
                                                     filterx_floating_variable_expr_new("d"));
  FilterXExpr *double_first = filterx_operator_plus_new(filterx_floating_variable_expr_new("d"),
                                                        filterx_floating_variable_expr_new("a"));
  FilterXExpr *block = filterx_compound_expr_new_va(TRUE,
                                                    _assign_literal("a", filterx_integer_new(1)),
                                                    _assign_literal("d", filterx_double_new(2.5)),
                                                    int_first, double_first, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(int_first->static_type, FILTERX_STATIC_TYPE_DOUBLE);
  cr_assert_eq(double_first->static_type, FILTERX_STATIC_TYPE_DOUBLE);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, plus_of_integer_and_unknown_is_unknown)
{
  /* a = 1; a + $FACILITY  ->  UNKNOWN: a macro may be a double at runtime, which widens the sum */
  FilterXExpr *sum = filterx_operator_plus_new(filterx_floating_variable_expr_new("a"),
                                               filterx_msg_variable_expr_new("FACILITY"));
  FilterXExpr *block = filterx_compound_expr_new_va(TRUE,
                                                    _assign_literal("a", filterx_integer_new(1)),
                                                    sum, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(sum->static_type, FILTERX_STATIC_TYPE_UNKNOWN);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, mixed_numeric_sum_does_not_poison_a_later_sum)
{
  /* a = 1; d = 2.5; x = a + d; x + a  ->  DOUBLE throughout */
  FilterXExpr *inner = filterx_operator_plus_new(filterx_floating_variable_expr_new("a"),
                                                 filterx_floating_variable_expr_new("d"));
  FilterXExpr *assign_x = filterx_assign_new(filterx_floating_variable_expr_new("x"), inner);
  FilterXExpr *outer = filterx_operator_plus_new(filterx_floating_variable_expr_new("x"),
                                                 filterx_floating_variable_expr_new("a"));
  FilterXExpr *block = filterx_compound_expr_new_va(TRUE,
                                                    _assign_literal("a", filterx_integer_new(1)),
                                                    _assign_literal("d", filterx_double_new(2.5)),
                                                    assign_x, outer, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(inner->static_type, FILTERX_STATIC_TYPE_DOUBLE);
  cr_assert_eq(outer->static_type, FILTERX_STATIC_TYPE_DOUBLE);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, folded_literal_dict_records_a_type_per_key)
{
  /* d = {"a": "x", "b": "y"} — fully literal, folds to a single FilterXDict literal, whose key
   * set is exactly known.  Each key gets an entry of its own; there is no one element type the
   * whole level has to agree on. */
  GList *elems = NULL;
  elems = g_list_append(elems, filterx_literal_element_new(
                          filterx_literal_new(filterx_string_new("a", -1)),
                          filterx_literal_new(filterx_string_new("x", -1))));
  elems = g_list_append(elems, filterx_literal_element_new(
                          filterx_literal_new(filterx_string_new("b", -1)),
                          filterx_literal_new(filterx_string_new("y", -1))));
  FilterXExpr *assign = filterx_assign_new(filterx_floating_variable_expr_new("d"),
                                           filterx_literal_dict_new(elems));
  FilterXExpr *read_a = _read_attr("d", "a");
  FilterXExpr *read_b = _read_attr("d", "b");
  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign, read_a, read_b, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(assign->static_type, FILTERX_STATIC_TYPE_DICT);
  cr_assert_eq(read_a->static_type, FILTERX_STATIC_TYPE_STRING);
  cr_assert_eq(read_b->static_type, FILTERX_STATIC_TYPE_STRING);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, folded_literal_dict_of_boolean_records_its_keys)
{
  /* d = {"a": true, "b": false} — fully literal, folds to a single FilterXDict literal. */
  GList *elems = NULL;
  elems = g_list_append(elems, filterx_literal_element_new(
                          filterx_literal_new(filterx_string_new("a", -1)),
                          filterx_literal_new(filterx_boolean_new(TRUE))));
  elems = g_list_append(elems, filterx_literal_element_new(
                          filterx_literal_new(filterx_string_new("b", -1)),
                          filterx_literal_new(filterx_boolean_new(FALSE))));
  FilterXExpr *assign = filterx_assign_new(filterx_floating_variable_expr_new("d"),
                                           filterx_literal_dict_new(elems));
  FilterXExpr *read_a = _read_attr("d", "a");
  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign, read_a, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(assign->static_type, FILTERX_STATIC_TYPE_DICT);
  cr_assert_eq(read_a->static_type, FILTERX_STATIC_TYPE_BOOLEAN);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, folded_literal_dict_chain_types_all_three_levels)
{
  /* d = {"l1": {"l2": {"l3": "leaf"}}}, fully literal; d.l1.l2.l3 reads back as a STRING. */
  GList *l3_elems = g_list_append(NULL, filterx_literal_element_new(
                                    filterx_literal_new(filterx_string_new("l3", -1)),
                                    filterx_literal_new(filterx_string_new("leaf", -1))));
  GList *l2_elems = g_list_append(NULL, filterx_literal_element_new(
                                    filterx_literal_new(filterx_string_new("l2", -1)),
                                    filterx_literal_dict_new(l3_elems)));
  GList *l1_elems = g_list_append(NULL, filterx_literal_element_new(
                                    filterx_literal_new(filterx_string_new("l1", -1)),
                                    filterx_literal_dict_new(l2_elems)));
  FilterXExpr *assign = filterx_assign_new(filterx_floating_variable_expr_new("d"),
                                           filterx_literal_dict_new(l1_elems));
  FilterXExpr *read_l1 = _read_attr("d", "l1");
  FilterXExpr *read_l2 = _read_attr_of(read_l1, "l2");
  FilterXExpr *read_l3 = _read_attr_of(read_l2, "l3");
  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign, read_l3, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(assign->static_type, FILTERX_STATIC_TYPE_DICT);
  cr_assert_eq(read_l1->static_type, FILTERX_STATIC_TYPE_DICT);
  cr_assert_eq(read_l2->static_type, FILTERX_STATIC_TYPE_DICT);
  cr_assert_eq(read_l3->static_type, FILTERX_STATIC_TYPE_STRING);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, mixed_value_types_in_literal_dict_stay_distinct_per_key)
{
  /* d = {"a": "x", "b": [1]} — a string and a list value.  Each key keeps its own type; only a
   * read that cannot name its key has to meet them, and there the two do not agree. */
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
  FilterXExpr *assign = filterx_assign_new(filterx_floating_variable_expr_new("d"),
                                           filterx_literal_dict_new(elems));
  FilterXExpr *read_a = _read_attr("d", "a");
  FilterXExpr *read_b = _read_attr("d", "b");
  FilterXExpr *read_dynamic = filterx_get_subscript_new(filterx_floating_variable_expr_new("d"),
                                                        filterx_floating_variable_expr_new("k"));
  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign, read_a, read_b, read_dynamic, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(assign->static_type, FILTERX_STATIC_TYPE_DICT);
  cr_assert_eq(read_a->static_type, FILTERX_STATIC_TYPE_STRING);
  cr_assert_eq(read_b->static_type, FILTERX_STATIC_TYPE_LIST);
  cr_assert_eq(read_dynamic->static_type, FILTERX_STATIC_TYPE_UNKNOWN);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, a_folded_literal_list_does_not_type_its_elements)
{
  /* v = {"l": [1, 2, 3]}; v.l[0]  ->  UNKNOWN, and v.l is still a LIST */
  GList *elems = NULL;
  for (gint i = 1; i <= 3; i++)
    elems = g_list_append(elems, filterx_literal_element_new(NULL,
                                                             filterx_literal_new(filterx_integer_new(i))));

  GList *outer = g_list_append(NULL, filterx_literal_element_new(
                                 filterx_literal_new(filterx_string_new("l", -1)),
                                 filterx_literal_list_new(elems)));
  FilterXExpr *assign_v = filterx_assign_new(filterx_floating_variable_expr_new("v"),
                                             filterx_literal_dict_new(outer));
  FilterXExpr *read_l = _read_attr("v", "l");
  FilterXExpr *read_elem = filterx_get_subscript_new(_read_attr("v", "l"), filterx_literal_new(filterx_integer_new(0)));

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_v, read_l, read_elem, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_l->static_type, FILTERX_STATIC_TYPE_LIST);
  cr_assert_eq(read_elem->static_type, FILTERX_STATIC_TYPE_UNKNOWN);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, literal_subscript_read_resolves_its_key)
{
  /* d = {"a": "x", "b": "y"}; d["a"]  ->  STRING */
  GList *elems = g_list_append(NULL, filterx_literal_element_new(
                                 filterx_literal_new(filterx_string_new("a", -1)),
                                 filterx_literal_new(filterx_string_new("x", -1))));
  elems = g_list_append(elems, filterx_literal_element_new(
                          filterx_literal_new(filterx_string_new("b", -1)),
                          filterx_literal_new(filterx_string_new("y", -1))));
  FilterXExpr *assign = filterx_assign_new(
                          filterx_floating_variable_expr_new("d"),
                          filterx_literal_dict_new(elems));
  FilterXExpr *read = filterx_get_subscript_new(
                        filterx_floating_variable_expr_new("d"),
                        filterx_literal_new(filterx_string_new("a", -1)));
  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign, read, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read->static_type, FILTERX_STATIC_TYPE_STRING);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, getattr_chain_propagates_through_three_levels)
{
  /* d = {"a": {"b": {"c": "leaf"}}}; d.a.b.c -> STRING, one path lookup per node. */
  GList *l3 = g_list_append(NULL, filterx_literal_element_new(
                              filterx_literal_new(filterx_string_new("c", -1)),
                              filterx_literal_new(filterx_string_new("leaf", -1))));
  GList *l2 = g_list_append(NULL, filterx_literal_element_new(
                              filterx_literal_new(filterx_string_new("b", -1)),
                              filterx_literal_dict_new(l3)));
  GList *l1 = g_list_append(NULL, filterx_literal_element_new(
                              filterx_literal_new(filterx_string_new("a", -1)),
                              filterx_literal_dict_new(l2)));
  FilterXExpr *assign = filterx_assign_new(
                          filterx_floating_variable_expr_new("d"),
                          filterx_literal_dict_new(l1));
  FilterXExpr *read_var = filterx_floating_variable_expr_new("d");
  FilterXExpr *get_a = _read_attr_of(read_var, "a");
  FilterXExpr *get_b = _read_attr_of(get_a, "b");
  FilterXExpr *get_c = _read_attr_of(get_b, "c");
  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign, get_c, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_var->static_type, FILTERX_STATIC_TYPE_DICT);
  cr_assert_eq(get_a->static_type, FILTERX_STATIC_TYPE_DICT);
  cr_assert_eq(get_b->static_type, FILTERX_STATIC_TYPE_DICT);
  cr_assert_eq(get_c->static_type, FILTERX_STATIC_TYPE_STRING);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, set_subscript_on_variable_leaves_the_other_keys_alone)
{
  /* d = {"a": "x"}; d["b"] = 42;  -> the write lands on one location.  Nothing is pessimized:
   * d.a is still a STRING and d.b is the integer just written. */
  GList *elems = g_list_append(NULL, filterx_literal_element_new(
                                 filterx_literal_new(filterx_string_new("a", -1)),
                                 filterx_literal_new(filterx_string_new("x", -1))));
  FilterXExpr *assign = filterx_assign_new(
                          filterx_floating_variable_expr_new("d"),
                          filterx_literal_dict_new(elems));
  FilterXExpr *set = filterx_set_subscript_new(
                       filterx_floating_variable_expr_new("d"),
                       filterx_literal_new(filterx_string_new("b", -1)),
                       filterx_literal_new(filterx_integer_new(42)));
  FilterXExpr *read = filterx_floating_variable_expr_new("d");
  FilterXExpr *read_a = _read_attr("d", "a");
  FilterXExpr *read_b = _read_attr("d", "b");
  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign, set, read, read_a, read_b, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(assign->static_type, FILTERX_STATIC_TYPE_DICT);
  cr_assert_eq(read->static_type, FILTERX_STATIC_TYPE_DICT);
  cr_assert_eq(read_a->static_type, FILTERX_STATIC_TYPE_STRING);
  cr_assert_eq(read_b->static_type, FILTERX_STATIC_TYPE_INTEGER);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, incremental_dict_build_tracks_nested_element_types)
{
  /* d = {}; d.a = {}; d.a.b = {};  -> reads of d, d.a and d.a.b all resolve to DICT: the
   * incremental build reaches the same state a nested literal would have, one path at a time. */
  FilterXExpr *assign_d = _assign_empty_dict("d");
  FilterXExpr *set_a = _write_attr("d", "a", filterx_literal_dict_new(NULL));
  FilterXExpr *set_ab = filterx_setattr_new(_read_attr("d", "a"), filterx_string_new("b", -1),
                                            filterx_literal_dict_new(NULL));

  FilterXExpr *read_d = filterx_floating_variable_expr_new("d");
  FilterXExpr *read_a = _read_attr("d", "a");
  FilterXExpr *read_ab = _read_attr_of(_read_attr("d", "a"), "b");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_d, set_a, set_ab,
                                                    read_d, read_a, read_ab, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_d->static_type, FILTERX_STATIC_TYPE_DICT);
  cr_assert_eq(read_a->static_type, FILTERX_STATIC_TYPE_DICT);
  cr_assert_eq(read_ab->static_type, FILTERX_STATIC_TYPE_DICT);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, per_key_type_survives_heterogeneous_sibling)
{
  /* d = {}; d.a = {}; d.b = "s";  -> the two keys disagree, and nothing forces them to agree:
   * each write records one location, so d.a stays a DICT and d.b a STRING. */
  FilterXExpr *assign_d = _assign_empty_dict("d");
  FilterXExpr *set_a = _write_attr("d", "a", filterx_literal_dict_new(NULL));
  FilterXExpr *set_b = _write_attr("d", "b", filterx_literal_new(filterx_string_new("s", -1)));
  FilterXExpr *read_a = _read_attr("d", "a");
  FilterXExpr *read_b = _read_attr("d", "b");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_d, set_a, set_b, read_a, read_b, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_a->static_type, FILTERX_STATIC_TYPE_DICT);
  cr_assert_eq(read_b->static_type, FILTERX_STATIC_TYPE_STRING);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, per_key_type_survives_non_container_sibling)
{
  /* d = {}; d.x = 5; d.y = {};  -> d.y is a DICT regardless of what the d.x sibling holds. */
  FilterXExpr *assign_d = _assign_empty_dict("d");
  FilterXExpr *set_x = _write_attr("d", "x", filterx_literal_new(filterx_integer_new(5)));
  FilterXExpr *set_y = _write_attr("d", "y", filterx_literal_dict_new(NULL));
  FilterXExpr *read_y = _read_attr("d", "y");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_d, set_x, set_y, read_y, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_y->static_type, FILTERX_STATIC_TYPE_DICT);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, a_list_element_has_no_type_of_its_own)
{
  /* l = []; l[0] = {}; l[0]  ->  UNKNOWN, and the list itself stays a LIST.
   *
   * A list index names no location: a negative one resolves against the runtime length, and
   * unset() shifts every later index down. */
  FilterXExpr *assign_l = filterx_assign_new(
                            filterx_floating_variable_expr_new("l"),
                            filterx_literal_list_new(NULL));
  FilterXExpr *set_0 = filterx_set_subscript_new(
                         filterx_floating_variable_expr_new("l"),
                         filterx_literal_new(filterx_integer_new(0)),
                         filterx_literal_dict_new(NULL));
  FilterXExpr *read_0 = filterx_get_subscript_new(
                          filterx_floating_variable_expr_new("l"),
                          filterx_literal_new(filterx_integer_new(0)));
  FilterXExpr *read_l = filterx_floating_variable_expr_new("l");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_l, set_0, read_0, read_l, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_0->static_type, FILTERX_STATIC_TYPE_UNKNOWN);
  cr_assert_eq(read_l->static_type, FILTERX_STATIC_TYPE_LIST);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, one_sided_branch_write_survives_the_join)
{
  /* d = {}; if (c) { d.a = {}; } d.a  ->  DICT.
   *
   * The branch that did not write has d closed, which proves the key absent there; a read of a
   * missing key returns C NULL, which static_type says nothing about. */
  FilterXExpr *assign_d = _assign_empty_dict("d");
  FilterXExpr *set_a = _write_attr("d", "a", filterx_literal_dict_new(NULL));
  FilterXExpr *iff = filterx_conditional_new(filterx_floating_variable_expr_new("c"));
  filterx_conditional_set_true_branch(iff, set_a);

  FilterXExpr *read_a = _read_attr("d", "a");
  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_d, iff, read_a, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_a->static_type, FILTERX_STATIC_TYPE_DICT);
  filterx_expr_unref(block);
}

/* Build a fresh getattr chain d.keys[0].keys[1]...keys[n-1]. */
static FilterXExpr *
_build_attr_chain(const gchar **keys, gint n)
{
  FilterXExpr *expr = filterx_floating_variable_expr_new("d");
  for (gint i = 0; i < n; i++)
    expr = filterx_getattr_new(expr, filterx_string_new(keys[i], -1));
  return expr;
}

Test(filterx_type_inference, incremental_build_past_depth_cap_is_safe)
{
  /* a write past FILTERX_ACCESS_PATH_MAX_DEPTH has no address to land on, so it opens the deepest
   * ancestor it can name */
  enum { DEPTH = 20 };
  cr_assert_gt(DEPTH, FILTERX_ACCESS_PATH_MAX_DEPTH);
  gchar key_storage[DEPTH][8];
  const gchar *keys[DEPTH];
  for (gint i = 0; i < DEPTH; i++)
    {
      g_snprintf(key_storage[i], sizeof(key_storage[i]), "k%d", i);
      keys[i] = key_storage[i];
    }

  FilterXExpr *block = filterx_compound_expr_new(TRUE);
  filterx_compound_expr_add_ref(block, _assign_empty_dict("d"));
  /* d.k0 = {}; d.k0.k1 = {}; ... */
  for (gint level = 0; level < DEPTH; level++)
    filterx_compound_expr_add_ref(block, filterx_setattr_new(
                                    _build_attr_chain(keys, level),
                                    filterx_string_new(keys[level], -1),
                                    filterx_literal_dict_new(NULL)));

  FilterXExpr *read_within_cap = _build_attr_chain(keys, 4);
  filterx_compound_expr_add_ref(block, read_within_cap);
  FilterXExpr *read_deepest = _build_attr_chain(keys, FILTERX_ACCESS_PATH_MAX_DEPTH);
  filterx_compound_expr_add_ref(block, read_deepest);

  block = _optimize_and_infer(block);

  cr_assert_eq(read_within_cap->static_type, FILTERX_STATIC_TYPE_DICT);
  cr_assert_eq(read_deepest->static_type, FILTERX_STATIC_TYPE_DICT);
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

/* A single-element literal dict {"<key>": "<value>"}, folded to one literal at optimize time. */
static FilterXExpr *
_string_valued_dict(const gchar *key, const gchar *value)
{
  GList *elements = g_list_append(NULL,
                                  filterx_literal_element_new(
                                    filterx_literal_new(filterx_string_new(key, -1)),
                                    filterx_literal_new(filterx_string_new(value, -1))));
  return filterx_literal_dict_new(elements);
}

Test(filterx_type_inference, merging_into_root_invalidates_stale_per_key_type)
{
  /* v = {}; v.a = 1;      -> v.a is an INTEGER
   * v += {"a": "boom"};   -> filterx_mapping_merge() is a shallow per-key overwrite, so key a is
   *                          now the string the right-hand side carried
   * v.a + 10              -> v.a must no longer resolve to INTEGER */
  FilterXExpr *assign_v = _assign_empty_dict("v");
  FilterXExpr *set_a = _write_attr("v", "a", filterx_literal_new(filterx_integer_new(1)));
  FilterXExpr *merge = filterx_operator_plus_assign_new(
                         filterx_floating_variable_expr_new("v"),
                         _string_valued_dict("a", "boom"));
  FilterXExpr *read_a = _read_attr("v", "a");
  FilterXExpr *sum = filterx_operator_plus_new(read_a, filterx_literal_new(filterx_integer_new(10)));

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_v, set_a, merge, sum, NULL);
  block = _optimize_and_infer(block);

  /* dict += dict keeps the outer static type, and the right-hand side's keys win. */
  cr_assert_eq(merge->static_type, FILTERX_STATIC_TYPE_DICT);
  cr_assert_eq(read_a->static_type, FILTERX_STATIC_TYPE_STRING);
  cr_assert_eq(sum->static_type, FILTERX_STATIC_TYPE_UNKNOWN);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, merging_into_nested_dict_invalidates_stale_per_key_type)
{
  /* d = {}; d.sub = {}; d.sub.a = 1;   -> d.sub.a is an INTEGER
   * d.sub += {"a": "boom"};            -> the merge target is a chain, not a bare variable
   * d.sub.a                            -> STRING: the merge overwrote key a, and the entry that
   *                                       claimed INTEGER must not survive it. */
  FilterXExpr *assign_d = _assign_empty_dict("d");
  FilterXExpr *set_sub = _write_attr("d", "sub", filterx_literal_dict_new(NULL));
  FilterXExpr *set_sub_a = filterx_setattr_new(_read_attr("d", "sub"), filterx_string_new("a", -1),
                                               filterx_literal_new(filterx_integer_new(1)));
  FilterXExpr *merge = filterx_operator_plus_assign_new(_read_attr("d", "sub"), _string_valued_dict("a", "boom"));
  FilterXExpr *read_sub_a = _read_attr_of(_read_attr("d", "sub"), "a");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_d, set_sub, set_sub_a, merge,
                                                    read_sub_a, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(merge->static_type, FILTERX_STATIC_TYPE_DICT);
  cr_assert_eq(read_sub_a->static_type, FILTERX_STATIC_TYPE_STRING);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, merging_into_a_key_keeps_sibling_per_key_types)
{
  /* d = {}; d.a = 1; d.sub = {}; d.sub += {"x": "s"};  -> the merge reaches only into its own
   * target, so the sibling d.a keeps its INTEGER type.  d.subscription pins that a shared
   * character prefix is not a shared path prefix: steps compare whole, not by strncmp(). */
  FilterXExpr *assign_d = _assign_empty_dict("d");
  FilterXExpr *set_a = _write_attr("d", "a", filterx_literal_new(filterx_integer_new(1)));
  FilterXExpr *set_subscription = _write_attr("d", "subscription", filterx_literal_new(filterx_string_new("s", -1)));
  FilterXExpr *set_sub = _write_attr("d", "sub", filterx_literal_dict_new(NULL));
  FilterXExpr *merge = filterx_operator_plus_assign_new(_read_attr("d", "sub"), _string_valued_dict("x", "s"));
  FilterXExpr *read_a = _read_attr("d", "a");
  FilterXExpr *read_subscription = _read_attr("d", "subscription");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_d, set_a, set_subscription, set_sub,
                                                    merge, read_a, read_subscription, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_a->static_type, FILTERX_STATIC_TYPE_INTEGER);
  cr_assert_eq(read_subscription->static_type, FILTERX_STATIC_TYPE_STRING);
  filterx_expr_unref(block);
}

/* v = {}; v.a = {}; v.a.b = 5;  -> v.a.b is an INTEGER
 * <replace v.a>;                -> whatever replaces the value at key a
 * v.a.b                         -> @expected_b, no longer INTEGER
 * Ownership of @replace_a is taken. */
static void
_assert_replacing_a_key_retypes_its_subtree(FilterXExpr *replace_a, FilterXStaticType expected_b)
{
  FilterXExpr *assign_v = _assign_empty_dict("v");
  FilterXExpr *set_a = _write_attr("v", "a", filterx_literal_dict_new(NULL));
  FilterXExpr *set_a_b = filterx_setattr_new(_read_attr("v", "a"), filterx_string_new("b", -1),
                                             filterx_literal_new(filterx_integer_new(5)));
  FilterXExpr *read_a_b = _read_attr_of(_read_attr("v", "a"), "b");
  FilterXExpr *sum = filterx_operator_plus_new(read_a_b, filterx_literal_new(filterx_integer_new(10)));

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_v, set_a, set_a_b, replace_a, sum, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_a_b->static_type, expected_b);
  cr_assert_eq(sum->static_type, FILTERX_STATIC_TYPE_UNKNOWN,
               "whatever v.a.b became, it is not an integer, so the sum cannot be one either");
  filterx_expr_unref(block);
}

Test(filterx_type_inference, setattr_replacing_a_key_installs_the_new_shape)
{
  /* v.a = {"b": "hopszika"};  -> the write names its key, so the old subtree is replaced by the
   * replacement's own: v.a.b is now exactly the string it holds. */
  _assert_replacing_a_key_retypes_its_subtree(_write_attr("v", "a", _string_valued_dict("b", "hopszika")),
                                              FILTERX_STATIC_TYPE_STRING);
}

Test(filterx_type_inference, literal_subscript_write_installs_the_new_shape)
{
  /* v["a"] = {"b": "hopszika"};  ->  the same store as v.a = ..., spelled with a literal key */
  _assert_replacing_a_key_retypes_its_subtree(
    filterx_set_subscript_new(filterx_floating_variable_expr_new("v"),
                              filterx_literal_new(filterx_string_new("a", -1)),
                              _string_valued_dict("b", "hopszika")),
    FILTERX_STATIC_TYPE_STRING);
}

Test(filterx_type_inference, dynamic_subscript_write_empties_the_container_it_hit)
{
  /* v[k] = {"b": "hopszika"};  -> k is only known at runtime, so the write may have landed on
   * key a.  Nothing under v survives it: v keeps DICT and loses every leaf, which is why the
   * read of v.a.b that follows has nothing to answer with. */
  _assert_replacing_a_key_retypes_its_subtree(
    filterx_set_subscript_new(filterx_floating_variable_expr_new("v"),
                              filterx_floating_variable_expr_new("k"),
                              _string_valued_dict("b", "hopszika")),
    FILTERX_STATIC_TYPE_UNKNOWN);
}

Test(filterx_type_inference, a_key_written_after_a_dynamic_write_keeps_its_type)
{
  /* d = {}; d[k] = "s"; d.a = 1; d.a  ->  INTEGER */
  FilterXExpr *assign_d = _assign_empty_dict("d");
  FilterXExpr *set_dynamic = filterx_set_subscript_new(
                               filterx_floating_variable_expr_new("d"),
                               filterx_floating_variable_expr_new("k"),
                               filterx_literal_new(filterx_string_new("s", -1)));
  FilterXExpr *set_a = _write_attr("d", "a", filterx_literal_new(filterx_integer_new(1)));
  FilterXExpr *read_a = _read_attr("d", "a");
  FilterXExpr *read_d = filterx_floating_variable_expr_new("d");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_d, set_dynamic, set_a, read_a, read_d, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_a->static_type, FILTERX_STATIC_TYPE_INTEGER);
  cr_assert_eq(read_d->static_type, FILTERX_STATIC_TYPE_DICT);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, a_dynamic_write_keeps_the_container_and_drops_its_leaves)
{
  /* d = {}; d.a = 1; d[k] = "s"; d.a  -> UNKNOWN, d -> DICT.  The write reached into d rather
   * than replacing it, so its static type stands; where inside it landed is not knowable, so nothing
   * that was recorded inside it does. */
  FilterXExpr *assign_d = _assign_empty_dict("d");
  FilterXExpr *set_a = _write_attr("d", "a", filterx_literal_new(filterx_integer_new(1)));
  FilterXExpr *set_dynamic = filterx_set_subscript_new(
                               filterx_floating_variable_expr_new("d"),
                               filterx_floating_variable_expr_new("k"),
                               filterx_literal_new(filterx_string_new("s", -1)));
  FilterXExpr *read_a = _read_attr("d", "a");
  FilterXExpr *read_d = filterx_floating_variable_expr_new("d");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_d, set_a, set_dynamic, read_a, read_d, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_a->static_type, FILTERX_STATIC_TYPE_UNKNOWN);
  cr_assert_eq(read_d->static_type, FILTERX_STATIC_TYPE_DICT);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, a_dynamic_write_one_level_down_leaves_its_siblings_alone)
{
  /* d.a[k] = 1 retires d.a's interior and nothing else: the write is addressed as far as d.a,
   * which is where the knowledge stops, not at the root variable. */
  FilterXExpr *assign_d = _assign_empty_dict("d");
  FilterXExpr *set_a = _write_attr("d", "a", filterx_literal_dict_new(NULL));
  FilterXExpr *set_a_x = filterx_setattr_new(_read_attr("d", "a"), filterx_string_new("x", -1),
                                             filterx_literal_new(filterx_integer_new(1)));
  FilterXExpr *set_sibling = _write_attr("d", "sib", filterx_literal_new(filterx_string_new("s", -1)));
  FilterXExpr *set_dynamic = filterx_set_subscript_new(_read_attr("d", "a"), filterx_floating_variable_expr_new("k"),
                                                       filterx_literal_new(filterx_integer_new(2)));
  FilterXExpr *read_a_x = _read_attr_of(_read_attr("d", "a"), "x");
  FilterXExpr *read_a = _read_attr("d", "a");
  FilterXExpr *read_sibling = _read_attr("d", "sib");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_d, set_a, set_a_x, set_sibling,
                                                    set_dynamic, read_a_x, read_a, read_sibling, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_a_x->static_type, FILTERX_STATIC_TYPE_UNKNOWN);
  cr_assert_eq(read_a->static_type, FILTERX_STATIC_TYPE_DICT);
  cr_assert_eq(read_sibling->static_type, FILTERX_STATIC_TYPE_STRING);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, replacing_a_key_keeps_sibling_per_key_types)
{
  /* v = {}; v.a = 1; v.sub = {}; v.sub.x = 2; v.sub = {"x": "s"};  -> replacing v.sub reaches
   * only its own subtree: the sibling v.a keeps INTEGER, and v.sub itself is still a DICT.
   * v.subscription pins that steps compare whole: it is not under v.sub. */
  FilterXExpr *assign_v = _assign_empty_dict("v");
  FilterXExpr *set_a = _write_attr("v", "a", filterx_literal_new(filterx_integer_new(1)));
  FilterXExpr *set_subscription = _write_attr("v", "subscription", filterx_literal_new(filterx_string_new("s", -1)));
  FilterXExpr *set_sub = _write_attr("v", "sub", filterx_literal_dict_new(NULL));
  FilterXExpr *set_sub_x = filterx_setattr_new(_read_attr("v", "sub"), filterx_string_new("x", -1),
                                               filterx_literal_new(filterx_integer_new(2)));
  FilterXExpr *replace_sub = _write_attr("v", "sub", _string_valued_dict("x", "s"));
  FilterXExpr *read_a = _read_attr("v", "a");
  FilterXExpr *read_subscription = _read_attr("v", "subscription");
  FilterXExpr *read_sub = _read_attr("v", "sub");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_v, set_a, set_subscription, set_sub,
                                                    set_sub_x, replace_sub, read_a, read_subscription,
                                                    read_sub, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_a->static_type, FILTERX_STATIC_TYPE_INTEGER);
  cr_assert_eq(read_subscription->static_type, FILTERX_STATIC_TYPE_STRING);
  cr_assert_eq(read_sub->static_type, FILTERX_STATIC_TYPE_DICT);
  filterx_expr_unref(block);
}

/* unset(@target) / move(@target) as a standalone expression; ownership of @target is taken. */
static FilterXExpr *
_unset_of(FilterXExpr *target)
{
  GError *err = NULL;
  GList *args = g_list_append(NULL, filterx_function_arg_new(NULL, target));
  FilterXExpr *unset = filterx_function_unset_new(filterx_function_args_new(args, &err), &err);
  cr_assert_null(err);
  cr_assert_not_null(unset);
  return unset;
}

static FilterXExpr *
_move_of(FilterXExpr *target)
{
  GError *err = NULL;
  GList *args = g_list_append(NULL, filterx_function_arg_new(NULL, target));
  FilterXExpr *move = filterx_function_move_new(filterx_function_args_new(args, &err), &err);
  cr_assert_null(err);
  cr_assert_not_null(move);
  return move;
}

Test(filterx_type_inference, unset_of_a_key_invalidates_its_subtree)
{
  /* v = {}; v.a = {}; v.a.b = 5; v.other = "s";  -> v.a is a DICT and v.a.b an INTEGER
   * unset(v.a); v.a, v.a.b                       -> key a is gone, so both must go quiet.
   *
   * unset() on a named key proves it gone, so this is the one drop that owes the container
   * nothing: v stays closed, over a smaller key set. */
  FilterXExpr *assign_v = _assign_empty_dict("v");
  FilterXExpr *set_a = _write_attr("v", "a", filterx_literal_dict_new(NULL));
  FilterXExpr *set_a_b = filterx_setattr_new(_read_attr("v", "a"), filterx_string_new("b", -1),
                                             filterx_literal_new(filterx_integer_new(5)));
  FilterXExpr *set_other = _write_attr("v", "other", filterx_literal_new(filterx_string_new("s", -1)));
  FilterXExpr *unset_a = _unset_of(_read_attr("v", "a"));
  FilterXExpr *read_a = _read_attr("v", "a");
  FilterXExpr *read_a_b = _read_attr_of(read_a, "b");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_v, set_a, set_a_b, set_other,
                                                    unset_a, read_a_b, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_a->static_type, FILTERX_STATIC_TYPE_UNKNOWN);
  cr_assert_eq(read_a_b->static_type, FILTERX_STATIC_TYPE_UNKNOWN);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, unset_of_a_variable_retires_everything_under_it)
{
  /* v = {}; v.a = 1; unset(v); v.a  -> the variable is the zero-step path, so retiring it is
   * the same range drop every other key gets; nothing under it survives. */
  FilterXExpr *assign_v = _assign_empty_dict("v");
  FilterXExpr *set_a = _write_attr("v", "a", filterx_literal_new(filterx_integer_new(1)));
  FilterXExpr *unset_v = _unset_of(filterx_floating_variable_expr_new("v"));
  FilterXExpr *read_v = filterx_floating_variable_expr_new("v");
  FilterXExpr *read_a = _read_attr("v", "a");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_v, set_a, unset_v, read_v, read_a, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_v->static_type, FILTERX_STATIC_TYPE_UNKNOWN);
  cr_assert_eq(read_a->static_type, FILTERX_STATIC_TYPE_UNKNOWN);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, unset_of_a_key_keeps_sibling_per_key_types)
{
  /* v = {}; v.a = 1; v.sub = {}; unset(v.sub); v.a  -> only the unset key's range goes. */
  FilterXExpr *assign_v = _assign_empty_dict("v");
  FilterXExpr *set_a = _write_attr("v", "a", filterx_literal_new(filterx_integer_new(1)));
  FilterXExpr *set_sub = _write_attr("v", "sub", filterx_literal_dict_new(NULL));
  FilterXExpr *unset_sub = _unset_of(_read_attr("v", "sub"));
  FilterXExpr *read_a = _read_attr("v", "a");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_v, set_a, set_sub, unset_sub,
                                                    read_a, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_a->static_type, FILTERX_STATIC_TYPE_INTEGER);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, move_returns_the_source_type_and_invalidates_it)
{
  /* v = {}; v.a = 1; v.other = {}; x = move(v.a);  -> x is an INTEGER (move hands back what it
   * took), while v.a stops being one, the key being gone. */
  FilterXExpr *assign_v = _assign_empty_dict("v");
  FilterXExpr *set_a = _write_attr("v", "a", filterx_literal_new(filterx_integer_new(1)));
  FilterXExpr *set_other = _write_attr("v", "other", filterx_literal_dict_new(NULL));
  FilterXExpr *move_a = _move_of(_read_attr("v", "a"));
  FilterXExpr *assign_x = filterx_assign_new(filterx_floating_variable_expr_new("x"), move_a);
  FilterXExpr *read_x = filterx_floating_variable_expr_new("x");
  FilterXExpr *read_a = _read_attr("v", "a");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_v, set_a, set_other, assign_x,
                                                    read_x, read_a, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_x->static_type, FILTERX_STATIC_TYPE_INTEGER);
  cr_assert_eq(read_a->static_type, FILTERX_STATIC_TYPE_UNKNOWN);
  filterx_expr_unref(block);
}

/* n = @seed; n += @addend; n  -> the static type the variable ends up with. */
static FilterXStaticType
_static_type_after_plus_assign(FilterXObject *seed, FilterXObject *addend)
{
  FilterXExpr *assign_n = filterx_assign_new(filterx_floating_variable_expr_new("n"),
                                             filterx_literal_new(seed));
  FilterXExpr *add = filterx_operator_plus_assign_new(filterx_floating_variable_expr_new("n"),
                                                      filterx_literal_new(addend));
  FilterXExpr *read_n = filterx_floating_variable_expr_new("n");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_n, add, read_n, NULL);
  block = _optimize_and_infer(block);

  FilterXStaticType static_type = read_n->static_type;
  cr_assert_eq(static_type, add->static_type,
               "the += expression's own type must match what the variable ends up with");
  filterx_expr_unref(block);
  return static_type;
}

Test(filterx_type_inference, plus_assign_promotes_a_numeric_pair)
{
  /* += widens the same way + does: adding a double to an integer leaves a double behind, which
   * a plain meet of the two static types would have discarded as UNKNOWN. */
  cr_assert_eq(_static_type_after_plus_assign(filterx_integer_new(1), filterx_integer_new(2)),
               FILTERX_STATIC_TYPE_INTEGER);
  cr_assert_eq(_static_type_after_plus_assign(filterx_integer_new(1), filterx_double_new(2.5)),
               FILTERX_STATIC_TYPE_DOUBLE);
  cr_assert_eq(_static_type_after_plus_assign(filterx_double_new(2.5), filterx_integer_new(1)),
               FILTERX_STATIC_TYPE_DOUBLE);
  cr_assert_eq(_static_type_after_plus_assign(filterx_double_new(2.5), filterx_double_new(1.5)),
               FILTERX_STATIC_TYPE_DOUBLE);
}

Test(filterx_type_inference, plus_assign_promotes_only_numeric_pairs)
{
  /* A non-numeric side has no numeric domain to promote in: string += string stays a STRING via
   * the meet, and a mixed pair collapses (`1 += "x"` is a runtime error). */
  cr_assert_eq(_static_type_after_plus_assign(filterx_string_new("a", -1), filterx_string_new("b", -1)),
               FILTERX_STATIC_TYPE_STRING);
  cr_assert_eq(_static_type_after_plus_assign(filterx_integer_new(1), filterx_string_new("x", -1)),
               FILTERX_STATIC_TYPE_UNKNOWN);
}

Test(filterx_type_inference, merging_into_an_empty_dict_commits_its_element_type)
{
  /* v = {}; v += {"a": "x"};  ->  v.a is the STRING the merge brought in, an empty dict being a
   * closed container with no keys */
  FilterXExpr *assign_v = _assign_empty_dict("v");
  FilterXExpr *merge = filterx_operator_plus_assign_new(filterx_floating_variable_expr_new("v"),
                                                        _string_valued_dict("a", "x"));
  FilterXExpr *read_v = filterx_floating_variable_expr_new("v");
  FilterXExpr *read_a = _read_attr("v", "a");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_v, merge, read_v, read_a, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_v->static_type, FILTERX_STATIC_TYPE_DICT);
  cr_assert_eq(read_a->static_type, FILTERX_STATIC_TYPE_STRING);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, merging_an_empty_dict_keeps_the_target_keys)
{
  /* v = {"a": "x"}; v += {};  -> a merge overwrites only the keys the right-hand side has, and
   * an empty one has none, so v.a is untouched. */
  FilterXExpr *assign_v = filterx_assign_new(filterx_floating_variable_expr_new("v"),
                                             _string_valued_dict("a", "x"));
  FilterXExpr *merge = filterx_operator_plus_assign_new(filterx_floating_variable_expr_new("v"),
                                                        filterx_literal_dict_new(NULL));
  FilterXExpr *read_v = filterx_floating_variable_expr_new("v");
  FilterXExpr *read_a = _read_attr("v", "a");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_v, merge, read_v, read_a, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_v->static_type, FILTERX_STATIC_TYPE_DICT);
  cr_assert_eq(read_a->static_type, FILTERX_STATIC_TYPE_STRING);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, merging_an_opened_dict_retires_the_target_keys)
{
  /* r = {}; r[k] = "s"; v = {}; v.a = 1; v += r;  -> v.a is UNKNOWN.
   *
   * r is a dict whose key set this pass cannot enumerate, so the merge may overwrite v.a with a
   * value nothing here has seen.  Only a closed right-hand side has keys that can be copied over
   * one by one and leave the rest of the target standing. */
  FilterXExpr *assign_r = _assign_empty_dict("r");
  FilterXExpr *open_r = filterx_set_subscript_new(
                          filterx_floating_variable_expr_new("r"),
                          filterx_floating_variable_expr_new("k"),
                          filterx_literal_new(filterx_string_new("s", -1)));
  FilterXExpr *assign_v = _assign_empty_dict("v");
  FilterXExpr *set_a = _write_attr("v", "a", filterx_literal_new(filterx_integer_new(1)));
  FilterXExpr *merge = filterx_operator_plus_assign_new(filterx_floating_variable_expr_new("v"),
                                                        filterx_floating_variable_expr_new("r"));
  FilterXExpr *read_a = _read_attr("v", "a");
  FilterXExpr *read_v = filterx_floating_variable_expr_new("v");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_r, open_r, assign_v, set_a, merge,
                                                    read_a, read_v, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_a->static_type, FILTERX_STATIC_TYPE_UNKNOWN);
  cr_assert_eq(read_v->static_type, FILTERX_STATIC_TYPE_DICT);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, writing_into_a_nested_empty_dict_commits_the_deeper_level)
{
  /* v = {}; v.a = {}; v.a = {"b": "x"};  ->  v.a.b reads back as a STRING */
  FilterXExpr *assign_v = _assign_empty_dict("v");
  FilterXExpr *set_a = _write_attr("v", "a", filterx_literal_dict_new(NULL));
  FilterXExpr *replace_a = _write_attr("v", "a", _string_valued_dict("b", "x"));
  FilterXExpr *read_v = filterx_floating_variable_expr_new("v");
  FilterXExpr *read_a = _read_attr("v", "a");
  FilterXExpr *read_a_b = _read_attr_of(read_a, "b");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_v, set_a, replace_a, read_v,
                                                    read_a_b, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_v->static_type, FILTERX_STATIC_TYPE_DICT);
  cr_assert_eq(read_a->static_type, FILTERX_STATIC_TYPE_DICT);
  cr_assert_eq(read_a_b->static_type, FILTERX_STATIC_TYPE_STRING);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, switch_meet_keeps_agreement_with_preswitch_state)
{
  /* z = "seed"; switch (c) { case "x": z = "a"; case "y": z = "b"; default: z = "c"; } z
   * ->  STRING.  A present default is not modeled, so the body's end-state is still met against
   * the pre-switch state. */
  FilterXExpr *seed_z = filterx_assign_new(
                          filterx_floating_variable_expr_new("z"),
                          filterx_literal_new(filterx_string_new("seed", -1)));

  FilterXExpr *selector = filterx_floating_variable_expr_new("c");
  GList *body = NULL;
  body = g_list_append(body, filterx_switch_case_new(filterx_literal_new(filterx_string_new("x", -1))));
  body = g_list_append(body, filterx_assign_new(filterx_floating_variable_expr_new("z"),
                                                filterx_literal_new(filterx_string_new("a", -1))));
  body = g_list_append(body, filterx_switch_case_new(filterx_literal_new(filterx_string_new("y", -1))));
  body = g_list_append(body, filterx_assign_new(filterx_floating_variable_expr_new("z"),
                                                filterx_literal_new(filterx_string_new("b", -1))));
  body = g_list_append(body, filterx_switch_case_new(NULL)); /* default */
  body = g_list_append(body, filterx_assign_new(filterx_floating_variable_expr_new("z"),
                                                filterx_literal_new(filterx_string_new("c", -1))));
  FilterXExpr *sw = filterx_switch_new(selector, body);

  FilterXExpr *read_z = filterx_floating_variable_expr_new("z");
  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, seed_z, sw, read_z, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_z->static_type, FILTERX_STATIC_TYPE_STRING);
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

Test(filterx_type_inference, getattr_and_a_literal_subscript_write_the_same_location)
{
  /* a.b = 1 and a["b"] = 1 are the same store, so either spelling has to be readable through the other */
  FilterXExpr *assign_getattr = _assign_empty_dict("a");
  FilterXExpr *set_getattr = _write_attr("a", "b", filterx_literal_new(filterx_integer_new(1)));
  FilterXExpr *read_via_subscript = filterx_get_subscript_new(
                                      filterx_floating_variable_expr_new("a"),
                                      filterx_literal_new(filterx_string_new("b", -1)));

  FilterXExpr *assign_subscript = _assign_empty_dict("c");
  FilterXExpr *set_subscript = filterx_set_subscript_new(
                                 filterx_floating_variable_expr_new("c"),
                                 filterx_literal_new(filterx_string_new("b", -1)),
                                 filterx_literal_new(filterx_integer_new(1)));
  FilterXExpr *read_via_getattr = _read_attr("c", "b");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_getattr, set_getattr,
                                                    assign_subscript, set_subscript,
                                                    read_via_subscript, read_via_getattr, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_via_subscript->static_type, FILTERX_STATIC_TYPE_INTEGER);
  cr_assert_eq(read_via_getattr->static_type, FILTERX_STATIC_TYPE_INTEGER);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, writing_a_key_twice_overwrites_rather_than_meets)
{
  /* a.b = "s"; a.b = 1;  -> INTEGER.  A write names one location, and the value that was there
   * is simply gone; meeting the two would answer UNKNOWN for a variable whose type is not in
   * doubt at all. */
  FilterXExpr *assign_a = _assign_empty_dict("a");
  FilterXExpr *set_string = _write_attr("a", "b", filterx_literal_new(filterx_string_new("s", -1)));
  FilterXExpr *set_integer = _write_attr("a", "b", filterx_literal_new(filterx_integer_new(1)));
  FilterXExpr *read_b = _read_attr("a", "b");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_a, set_string, set_integer,
                                                    read_b, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_b->static_type, FILTERX_STATIC_TYPE_INTEGER);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, a_write_below_a_dynamic_subscript_retires_the_container_above_it)
{
  /* d = {}; d.a = {}; d.a.x = 1; d[k].b = 2; d.a.x  ->  UNKNOWN: the setattr's object is d[k],
   * which addresses no location, so the path it hands the env stops truncated at d. */
  FilterXExpr *assign_d = _assign_empty_dict("d");
  FilterXExpr *set_a = _write_attr("d", "a", filterx_literal_dict_new(NULL));
  FilterXExpr *set_a_x = filterx_setattr_new(_read_attr("d", "a"), filterx_string_new("x", -1),
                                             filterx_literal_new(filterx_integer_new(1)));
  FilterXExpr *set_dynamic_b = filterx_setattr_new(
                                 filterx_get_subscript_new(filterx_floating_variable_expr_new("d"),
                                                           filterx_floating_variable_expr_new("k")),
                                 filterx_string_new("b", -1),
                                 filterx_literal_new(filterx_integer_new(2)));
  FilterXExpr *read_a_x = _read_attr_of(_read_attr("d", "a"), "x");
  FilterXExpr *read_d = filterx_floating_variable_expr_new("d");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_d, set_a, set_a_x, set_dynamic_b,
                                                    read_a_x, read_d, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_a_x->static_type, FILTERX_STATIC_TYPE_UNKNOWN);
  cr_assert_eq(read_d->static_type, FILTERX_STATIC_TYPE_DICT);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, a_dynamic_read_has_no_location_to_read)
{
  /* a = {}; a.x = 1; a[k]  ->  UNKNOWN even though a is closed over a single INTEGER key: a[k]
   * addresses no location, so no entry speaks for it.  The named read a.x is unaffected. */
  FilterXExpr *assign_a = _assign_empty_dict("a");
  FilterXExpr *set_x = _write_attr("a", "x", filterx_literal_new(filterx_integer_new(1)));
  FilterXExpr *read_dynamic = filterx_get_subscript_new(filterx_floating_variable_expr_new("a"),
                                                        filterx_floating_variable_expr_new("k"));
  FilterXExpr *read_x = _read_attr("a", "x");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_a, set_x, read_dynamic, read_x, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_dynamic->static_type, FILTERX_STATIC_TYPE_UNKNOWN);
  cr_assert_eq(read_x->static_type, FILTERX_STATIC_TYPE_INTEGER);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, a_branch_write_over_an_opened_container_does_not_survive_the_join)
{
  /* d = {}; d[k] = "s"; if (c) { d.a = 1; } d.a
   *
   * The dynamic write leaves d open, so the branch that did not write cannot prove "a" absent --
   * d may well hold the "s" the dynamic write put there.  The written type therefore has to go at
   * the join, unlike in `d = {}; if (c) { d.a = 1; }` where d stays closed. */
  FilterXExpr *assign_d = _assign_empty_dict("d");
  FilterXExpr *set_dynamic = filterx_set_subscript_new(
                               filterx_floating_variable_expr_new("d"),
                               filterx_floating_variable_expr_new("k"),
                               filterx_literal_new(filterx_string_new("s", -1)));
  FilterXExpr *set_a = _write_attr("d", "a", filterx_literal_new(filterx_integer_new(1)));
  FilterXExpr *iff = filterx_conditional_new(filterx_floating_variable_expr_new("c"));
  filterx_conditional_set_true_branch(iff, set_a);

  FilterXExpr *read_a = _read_attr("d", "a");
  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_d, set_dynamic, iff, read_a, NULL);
  block = _optimize_and_infer(block);

  cr_assert_neq(read_a->static_type, FILTERX_STATIC_TYPE_STRING);
  cr_assert_eq(read_a->static_type, FILTERX_STATIC_TYPE_UNKNOWN);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, copying_a_container_into_its_own_subtree_terminates)
{
  /* d.sub = d;  -- the range being read is the range being written.  _emit_setattr_call() forks
   * the RHS first for exactly this statement, and the source has to be snapshotted before the
   * destination is dropped for the same reason. */
  FilterXExpr *assign_d = _assign_empty_dict("d");
  FilterXExpr *set_a = _write_attr("d", "a", filterx_literal_new(filterx_integer_new(1)));
  FilterXExpr *self_copy = _write_attr("d", "sub", filterx_floating_variable_expr_new("d"));
  FilterXExpr *read_sub = _read_attr("d", "sub");
  FilterXExpr *read_sub_a = _read_attr_of(_read_attr("d", "sub"), "a");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_d, set_a, self_copy,
                                                    read_sub, read_sub_a, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_sub->static_type, FILTERX_STATIC_TYPE_DICT);
  cr_assert_eq(read_sub_a->static_type, FILTERX_STATIC_TYPE_INTEGER);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, hoisting_a_subtree_over_its_own_root_terminates)
{
  /* d = d.sub;  -- the mirror hazard: without snapshotting first, the copy destroys its source
   * before it has read it. */
  FilterXExpr *assign_d = _assign_empty_dict("d");
  FilterXExpr *set_sub = _write_attr("d", "sub", filterx_literal_dict_new(NULL));
  FilterXExpr *set_sub_a = filterx_setattr_new(_read_attr("d", "sub"), filterx_string_new("a", -1),
                                               filterx_literal_new(filterx_string_new("s", -1)));
  FilterXExpr *hoist = filterx_assign_new(filterx_floating_variable_expr_new("d"), _read_attr("d", "sub"));
  FilterXExpr *read_a = _read_attr("d", "a");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_d, set_sub, set_sub_a, hoist,
                                                    read_a, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_a->static_type, FILTERX_STATIC_TYPE_STRING);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, a_non_literal_key_leaves_the_container_empty)
{
  /* d = {k: 1};  -- a non-literal key fails filterx_mapping_normalize_key(), so the optimizer
   * builds no template and there is no key to hang the 1 on.  The container's own static type is all
   * that survives; every key of it reads UNKNOWN, including the one it does hold. */
  GList *elems = g_list_append(NULL, filterx_literal_element_new(
                                 filterx_floating_variable_expr_new("k"),
                                 filterx_literal_new(filterx_integer_new(1))));
  FilterXExpr *assign_d = filterx_assign_new(filterx_floating_variable_expr_new("d"),
                                             filterx_literal_dict_new(elems));
  FilterXExpr *read_named = _read_attr("d", "anything");
  FilterXExpr *read_d = filterx_floating_variable_expr_new("d");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_d, read_named, read_d, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_named->static_type, FILTERX_STATIC_TYPE_UNKNOWN);
  cr_assert_eq(read_d->static_type, FILTERX_STATIC_TYPE_DICT);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, a_nullv_element_records_no_key_at_all)
{
  /* d = {"a": ??x};  -- _literal_dict_store_elem() leaves the slot unset when the value is null,
   * so the key may not exist at runtime.  Recording it would assert that it does; leaving no
   * entry is what makes a read of d.a answer UNKNOWN rather than x's type. */
  GList *elems = g_list_append(NULL, filterx_nullv_literal_element_new(
                                 filterx_literal_new(filterx_string_new("a", -1)),
                                 filterx_floating_variable_expr_new("x")));
  FilterXExpr *assign_x = filterx_assign_new(filterx_floating_variable_expr_new("x"),
                                             filterx_literal_new(filterx_string_new("s", -1)));
  FilterXExpr *assign_d = filterx_assign_new(filterx_floating_variable_expr_new("d"),
                                             filterx_literal_dict_new(elems));
  FilterXExpr *read_a = _read_attr("d", "a");

  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_x, assign_d, read_a, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_a->static_type, FILTERX_STATIC_TYPE_UNKNOWN);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, a_nullv_element_costs_the_container_its_closed_key_set)
{
  /* d = {"a": ??x}; if (c) { d.a = 1; } d.a  ->  UNKNOWN.
   *
   * The nullv key is recorded nowhere but may exist at runtime, so d cannot stay closed: a closed
   * d would let the branch that did not write prove "a" absent and the join would keep INTEGER. */
  GList *elems = g_list_append(NULL, filterx_nullv_literal_element_new(
                                 filterx_literal_new(filterx_string_new("a", -1)),
                                 filterx_floating_variable_expr_new("x")));
  FilterXExpr *assign_x = filterx_assign_new(filterx_floating_variable_expr_new("x"),
                                             filterx_literal_new(filterx_string_new("s", -1)));
  FilterXExpr *assign_d = filterx_assign_new(filterx_floating_variable_expr_new("d"),
                                             filterx_literal_dict_new(elems));
  FilterXExpr *set_a = _write_attr("d", "a", filterx_literal_new(filterx_integer_new(1)));
  FilterXExpr *iff = filterx_conditional_new(filterx_floating_variable_expr_new("c"));
  filterx_conditional_set_true_branch(iff, set_a);

  FilterXExpr *read_a = _read_attr("d", "a");
  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_x, assign_d, iff, read_a, NULL);
  block = _optimize_and_infer(block);

  cr_assert_eq(read_a->static_type, FILTERX_STATIC_TYPE_UNKNOWN);
  filterx_expr_unref(block);
}

Test(filterx_type_inference, env_entries_outlive_the_expression_tree_they_came_from)
{
  /* FilterXExpr::name borrows its characters from the string object the getattr owns, so an env
   * that did not intern its keys would dangle once the tree is freed: here the tree goes first. */
  FilterXExpr *variable = filterx_floating_variable_expr_new("v");
  FilterXVariableHandle handle = filterx_variable_expr_get_handle(variable);
  FilterXExpr *assign_v = filterx_assign_new(variable, filterx_literal_dict_new(NULL));
  FilterXExpr *set_key = _write_attr("v", "a_borrowed_key_name", filterx_literal_new(filterx_string_new("s", -1)));
  FilterXExpr *block = filterx_compound_expr_new_va(TRUE, assign_v, set_key, NULL);

  FilterXTypeEnv *env = filterx_type_env_new();
  block = filterx_expr_optimize(block);
  filterx_expr_infer_types(block, env);

  filterx_expr_unref(block);

  FilterXAccessPath path;
  memset(&path, 0, sizeof(path));
  path.root = handle;
  filterx_access_path_append_step(&path, filterx_access_path_intern_key("a_borrowed_key_name"));

  cr_assert_eq(filterx_type_env_get_static_type_at_path(env, &path), FILTERX_STATIC_TYPE_STRING);
  filterx_type_env_free(env);
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
