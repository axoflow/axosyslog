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
