/*
 * Copyright (c) 2025 Tamas Kosztyu <tamas.kosztyu@axoflow.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */
#include "expr-arithmetic-operators.h"

typedef struct FilterXArithmeticOperator_
{
  FilterXBinaryOp super;
} FilterXArithmeticOperator;

static FilterXObject *
_eval_substraction(FilterXExpr *s)
{
  return NULL;
}

static FilterXExpr *
_optimize_substraction(FilterXExpr *s)
{
  return NULL;
}

FilterXExpr *
filterx_arithmetic_operator_substraction_new(FilterXExpr *lhs, FilterXExpr *rhs)
{
  FilterXArithmeticOperator *self = g_new0(FilterXArithmeticOperator, 1);
  filterx_binary_op_init_instance(&self->super, "subs", lhs, rhs);
  self->super.super.optimize = _optimize_substraction;
  self->super.super.eval = _eval_substraction;

  return &self->super.super;
}

static FilterXObject *
_eval_multiplication(FilterXExpr *s)
{
  return NULL;
}

static FilterXExpr *
_optimize_multiplication(FilterXExpr *s)
{
  return NULL;
}

FilterXExpr *
filterx_arithmetic_operator_multiplication_new(FilterXExpr *lhs, FilterXExpr *rhs)
{
  FilterXArithmeticOperator *self = g_new0(FilterXArithmeticOperator, 1);
  filterx_binary_op_init_instance(&self->super, "mult", lhs, rhs);
  self->super.super.optimize = _optimize_multiplication;
  self->super.super.eval = _eval_multiplication;

  return &self->super.super;
}

static FilterXObject *
_eval_division(FilterXExpr *s)
{
  return NULL;
}

static FilterXExpr *
_optimize_division(FilterXExpr *s)
{
  return NULL;
}

FilterXExpr *
filterx_arithmetic_operator_division_new(FilterXExpr *lhs, FilterXExpr *rhs)
{
  FilterXArithmeticOperator *self = g_new0(FilterXArithmeticOperator, 1);
  filterx_binary_op_init_instance(&self->super, "subs", lhs, rhs);
  self->super.super.optimize = _optimize_division;
  self->super.super.eval = _eval_division;

  return &self->super.super;
}

static FilterXObject *
_eval_modulo(FilterXExpr *s)
{
  return NULL;
}

static FilterXExpr *
_optimize_modulo(FilterXExpr *s)
{
  return NULL;
}

FilterXExpr *
filterx_arithmetic_operator_modulo_new(FilterXExpr *lhs, FilterXExpr *rhs)
{
  FilterXArithmeticOperator *self = g_new0(FilterXArithmeticOperator, 1);
  filterx_binary_op_init_instance(&self->super, "mod", lhs, rhs);
  self->super.super.optimize = _optimize_modulo;
  self->super.super.eval = _eval_modulo;

  return &self->super.super;
}
