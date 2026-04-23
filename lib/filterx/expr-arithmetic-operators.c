/*
 * Copyright (c) 2025 Tamas Kosztyu <tamas.kosztyu@axoflow.com>
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
#include "expr-arithmetic-operators.h"
#include "filterx/object-primitive.h"
#include "filterx/expr-literal.h"
#include "filterx/object-extractor.h"
#include "filterx/filterx-eval.h"

/* If you want to make these operators support other types,
 * remove these implementations, look at expr-plus.c
 * and follow the way it was implemented. */

typedef struct FilterXArithmeticOperator_
{
  FilterXBinaryOp super;
  FilterXObject *literal_lhs;
  FilterXObject *literal_rhs;
} FilterXArithmeticOperator;


/* consumes operand objects */
static gboolean
_extract_operands_into_generic_numbers(FilterXObject *lhs_object, FilterXObject *rhs_object,
                 GenericNumber *lhs_number, GenericNumber *rhs_number, FilterXExpr *expr)
{
  gboolean ok = FALSE;

  if (!lhs_object)
    {
      filterx_eval_push_error_static_info("Failed to evaluate arithmetic operator", expr,
                                          "Failed to evaluate left hand side");
      goto exit;
    }
  if (!filterx_object_extract_generic_number(lhs_object, lhs_number))
    {
      filterx_eval_push_error_info_printf("Failed to evaluate arithmetic operator", expr,
                                          "Left hand side must be a double or integer, got: %s",
                                          filterx_object_get_type_name(lhs_object));
      goto exit;
    }
  if (!rhs_object)
    {
      filterx_eval_push_error_static_info("Failed to evaluate arithmetic operator", expr,
                                          "Failed to evaluate right hand side");
      goto exit;
    }
  if (!filterx_object_extract_generic_number(rhs_object, rhs_number))
    {
      filterx_eval_push_error_info_printf("Failed to evaluate arithmetic operator", expr,
                                          "right hand side must be a double or integer, got: %s",
                                          filterx_object_get_type_name(rhs_object));
      goto exit;
    }
  ok = TRUE;

exit:
  filterx_object_unref(lhs_object);
  filterx_object_unref(rhs_object);
  return ok;
}

static inline FilterXObject *
_eval_lhs(FilterXArithmeticOperator *self)
{
  return self->literal_lhs ? filterx_object_ref(self->literal_lhs) : filterx_expr_eval_typed(self->super.lhs);
}

static inline FilterXObject *
_eval_rhs(FilterXArithmeticOperator *self)
{
  return self->literal_rhs ? filterx_object_ref(self->literal_rhs) : filterx_expr_eval(self->super.rhs);
}

static void
_optimize_arithmetic_operators_common(FilterXArithmeticOperator *self)
{
  if (filterx_expr_is_literal(self->super.lhs))
    self->literal_lhs = filterx_literal_get_value(self->super.lhs);

  if (filterx_expr_is_literal(self->super.rhs))
    self->literal_rhs = filterx_literal_get_value(self->super.rhs);
}

static void
_free_arithmetic_common(FilterXExpr *s)
{
  FilterXArithmeticOperator *self = (FilterXArithmeticOperator *) s;
  filterx_object_unref(self->literal_lhs);
  filterx_object_unref(self->literal_rhs);

  filterx_binary_op_free_method(s);
}

static FilterXObject *
_do_substraction(FilterXObject *lhs, FilterXObject *rhs, FilterXExpr *expr)
{
  GenericNumber lhs_number, rhs_number, result;

  if (!_extract_operands_into_generic_numbers(lhs, rhs, &lhs_number, &rhs_number, expr))
    return NULL;

  if (lhs_number.type == GN_NAN || rhs_number.type == GN_NAN)
    return NULL;

  if (lhs_number.type == GN_INT64 && rhs_number.type == GN_INT64)
    {
      gn_set_int64(&result, gn_as_int64(&lhs_number) - gn_as_int64(&rhs_number));
      return filterx_integer_new(gn_as_int64(&result));
    }

  gn_set_double(&result, gn_as_double(&lhs_number) - gn_as_double(&rhs_number), -1);
  return filterx_double_new(gn_as_double(&result));
}

static FilterXObject *
_eval_substraction(FilterXExpr *s)
{
  FilterXArithmeticOperator *self = (FilterXArithmeticOperator *) s;
  return _do_substraction(_eval_lhs(self), _eval_rhs(self), s);
}

static FilterXExpr *
_optimize_substraction(FilterXExpr *s)
{
  FilterXArithmeticOperator *self = (FilterXArithmeticOperator *) s;

  _optimize_arithmetic_operators_common(self);

  if (self->literal_lhs && self->literal_rhs)
    return filterx_literal_new(_eval_substraction(&self->super.super));
  return NULL;
}

static FilterXObject *
_do_multiplication(FilterXObject *lhs, FilterXObject *rhs, FilterXExpr *expr)
{
  GenericNumber lhs_number, rhs_number, result;

  if (!_extract_operands_into_generic_numbers(lhs, rhs, &lhs_number, &rhs_number, expr))
    return NULL;

  if (lhs_number.type == GN_NAN || rhs_number.type == GN_NAN)
    return NULL;

  if (lhs_number.type == GN_INT64 && rhs_number.type == GN_INT64)
    {
      gn_set_int64(&result, gn_as_int64(&lhs_number) * gn_as_int64(&rhs_number));
      return filterx_integer_new(gn_as_int64(&result));
    }

  gn_set_double(&result, gn_as_double(&lhs_number) * gn_as_double(&rhs_number), -1);
  return filterx_double_new(gn_as_double(&result));
}

static FilterXObject *
_eval_multiplication(FilterXExpr *s)
{
  FilterXArithmeticOperator *self = (FilterXArithmeticOperator *) s;
  return _do_multiplication(_eval_lhs(self), _eval_rhs(self), s);
}

static FilterXExpr *
_optimize_multiplication(FilterXExpr *s)
{
  FilterXArithmeticOperator *self = (FilterXArithmeticOperator *) s;

  _optimize_arithmetic_operators_common(self);

  if (self->literal_lhs && self->literal_rhs)
    return filterx_literal_new(_eval_multiplication(&self->super.super));
  return NULL;
}

static FilterXObject *
_do_division(FilterXObject *lhs, FilterXObject *rhs, FilterXExpr *expr)
{
  GenericNumber lhs_number, rhs_number, result;

  if (!_extract_operands_into_generic_numbers(lhs, rhs, &lhs_number, &rhs_number, expr))
    return NULL;

  if (lhs_number.type == GN_NAN || rhs_number.type == GN_NAN)
    return NULL;

  if (lhs_number.type == GN_INT64 && rhs_number.type == GN_INT64)
    {
      gn_set_int64(&result, gn_as_int64(&lhs_number) / gn_as_int64(&rhs_number));
      return filterx_integer_new(gn_as_int64(&result));
    }

  gn_set_double(&result, gn_as_double(&lhs_number) / gn_as_double(&rhs_number), -1);
  return filterx_double_new(gn_as_double(&result));
}

static FilterXObject *
_eval_division(FilterXExpr *s)
{
  FilterXArithmeticOperator *self = (FilterXArithmeticOperator *) s;
  return _do_division(_eval_lhs(self), _eval_rhs(self), s);
}

static FilterXExpr *
_optimize_division(FilterXExpr *s)
{
  FilterXArithmeticOperator *self = (FilterXArithmeticOperator *) s;

  _optimize_arithmetic_operators_common(self);

  if (self->literal_lhs && self->literal_rhs)
    return filterx_literal_new(_eval_division(&self->super.super));
  return NULL;
}

static FilterXObject *
_do_modulo(FilterXObject *lhs, FilterXObject *rhs, FilterXExpr *expr)
{
  gint64 lhs_number, rhs_number;
  FilterXObject *result = NULL;

  if (!lhs)
    {
      filterx_eval_push_error_static_info("Failed to evaluate modulo operator", expr,
                                          "Failed to evaluate left hand side");
      goto exit;
    }

  if (!filterx_object_extract_integer(lhs, &lhs_number))
    {
      filterx_eval_push_error_info_printf("Failed to evaluate modulo operator", expr,
                                          "Left hand side must be an integer, got: %s",
                                          filterx_object_get_type_name(lhs));
      goto exit;
    }

  if (!rhs)
    {
      filterx_eval_push_error_static_info("Failed to evaluate modulo operator", expr,
                                          "Failed to evaluate right hand side");
      goto exit;
    }

  if (!filterx_object_extract_integer(rhs, &rhs_number))
    {
      filterx_eval_push_error_info_printf("Failed to evaluate modulo operator", expr,
                                          "Right hand side must be an integer, got: %s",
                                          filterx_object_get_type_name(rhs));
      goto exit;
    }
  result = filterx_integer_new(lhs_number % rhs_number);

exit:
  filterx_object_unref(lhs);
  filterx_object_unref(rhs);
  return result;
}

static FilterXObject *
_eval_modulo(FilterXExpr *s)
{
  FilterXArithmeticOperator *self = (FilterXArithmeticOperator *) s;
  return _do_modulo(_eval_lhs(self), _eval_rhs(self), s);
}

static FilterXExpr *
_optimize_modulo(FilterXExpr *s)
{
  FilterXArithmeticOperator *self = (FilterXArithmeticOperator *) s;

  _optimize_arithmetic_operators_common(self);

  if (self->literal_lhs && self->literal_rhs)
    return filterx_literal_new(_eval_modulo(&self->super.super));
  return NULL;
}

static FilterXObject *
_do_uminus(FilterXObject *operand_obj, FilterXExpr *expr)
{
  GenericNumber operand, result;
  FilterXObject *out = NULL;

  if (!operand_obj)
    {
      filterx_eval_push_error_static_info("Failed to evaluate arithmetic operator", expr,
                                          "Failed to evaluate operand");
      goto exit;
    }

  if (!filterx_object_extract_generic_number(operand_obj, &operand))
    {
      filterx_eval_push_error_info_printf("Failed to evaluate arithmetic operator", expr,
                                          "Operand must be a double or integer, got: %s",
                                          filterx_object_get_type_name(operand_obj));
      goto exit;
    }

  if (operand.type == GN_NAN)
    goto exit;

  if (operand.type == GN_INT64)
    {
      gn_set_int64(&result, -gn_as_int64(&operand));
      out = filterx_integer_new(gn_as_int64(&result));
      goto exit;
    }

  gn_set_double(&result, -gn_as_double(&operand), -1);
  out = filterx_double_new(gn_as_double(&result));

exit:
  filterx_object_unref(operand_obj);
  return out;
}

static FilterXObject *
_eval_uminus(FilterXExpr *s)
{
  FilterXUnaryOp *self = (FilterXUnaryOp *) s;
  return _do_uminus(filterx_expr_eval_typed(self->operand), &self->super);
}

FilterXExpr *
filterx_arithmetic_operator_substraction_new(FilterXExpr *lhs, FilterXExpr *rhs)
{
  FilterXArithmeticOperator *self = g_new0(FilterXArithmeticOperator, 1);
  filterx_binary_op_init_instance(&self->super, "subs", FXE_READ, lhs, rhs);
  self->super.super.optimize = _optimize_substraction;
  self->super.super.eval = _eval_substraction;
  self->super.super.free_fn = _free_arithmetic_common;

  return &self->super.super;
}

FilterXExpr *
filterx_arithmetic_operator_division_new(FilterXExpr *lhs, FilterXExpr *rhs)
{
  FilterXArithmeticOperator *self = g_new0(FilterXArithmeticOperator, 1);
  filterx_binary_op_init_instance(&self->super, "subs", FXE_READ, lhs, rhs);
  self->super.super.optimize = _optimize_division;
  self->super.super.eval = _eval_division;
  self->super.super.free_fn = _free_arithmetic_common;

  return &self->super.super;
}

FilterXExpr *
filterx_arithmetic_operator_modulo_new(FilterXExpr *lhs, FilterXExpr *rhs)
{
  FilterXArithmeticOperator *self = g_new0(FilterXArithmeticOperator, 1);
  filterx_binary_op_init_instance(&self->super, "mod", FXE_READ, lhs, rhs);
  self->super.super.optimize = _optimize_modulo;
  self->super.super.eval = _eval_modulo;
  self->super.super.free_fn = _free_arithmetic_common;

  return &self->super.super;
}

FilterXExpr *
filterx_arithmetic_operator_multiplication_new(FilterXExpr *lhs, FilterXExpr *rhs)
{
  FilterXArithmeticOperator *self = g_new0(FilterXArithmeticOperator, 1);
  filterx_binary_op_init_instance(&self->super, "mult", FXE_READ, lhs, rhs);
  self->super.super.optimize = _optimize_multiplication;
  self->super.super.eval = _eval_multiplication;
  self->super.super.free_fn = _free_arithmetic_common;

  return &self->super.super;
}

FilterXExpr *
filterx_arithmetic_operator_uminus_new(FilterXExpr *operand)
{
  FilterXUnaryOp *self = g_new0(FilterXUnaryOp, 1);
  filterx_unary_op_init_instance(self, "uminus", FXE_READ, operand);

  self->super.eval = _eval_uminus;

  return &self->super;
}
