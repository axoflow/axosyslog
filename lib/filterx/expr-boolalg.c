/*
 * Copyright (c) 2023 Balazs Scheidler <balazs.scheidler@axoflow.com>
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
#include "filterx/expr-boolalg.h"
#include "filterx/object-primitive.h"
#include "filterx/expr-literal.h"
#include "filterx/filterx-eval.h"

static inline gboolean
_literal_expr_truthy(FilterXExpr *expr)
{
  FilterXObject *result = filterx_literal_get_value(expr);
  g_assert(result);

  gboolean truthy = filterx_object_truthy(result);
  filterx_object_unref(result);

  return truthy;
}

static FilterXExpr *
_optimize_not(FilterXExpr *s)
{
  FilterXUnaryOp *self = (FilterXUnaryOp *) s;

  if (filterx_unary_op_optimize_method(s))
    g_assert_not_reached();

  if (!filterx_expr_is_literal(self->operand))
    return NULL;

  return filterx_literal_new(filterx_boolean_new(!_literal_expr_truthy(self->operand)));
}

static FilterXObject *
_eval_not(FilterXExpr *s)
{
  FilterXUnaryOp *self = (FilterXUnaryOp *) s;

  FilterXObject *result = filterx_expr_eval(self->operand);
  if (!result)
    {
      filterx_eval_push_error_static_info("Failed to negate expression", s, "Failed to evaluate expression");
      return NULL;
    }

  gboolean truthy = filterx_object_truthy(result);
  filterx_object_unref(result);

  if (truthy)
    return filterx_boolean_new(FALSE);
  else
    return filterx_boolean_new(TRUE);
}

FilterXExpr *
filterx_unary_not_new(FilterXExpr *operand)
{
  FilterXUnaryOp *self = g_new0(FilterXUnaryOp, 1);

  filterx_unary_op_init_instance(self, "not", FALSE, operand);
  self->super.optimize = _optimize_not;
  self->super.eval = _eval_not;
  return &self->super;
}

static FilterXExpr *
_optimize_and(FilterXExpr *s)
{
  FilterXBinaryOp *self = (FilterXBinaryOp *) s;

  if (filterx_binary_op_optimize_method(s))
    g_assert_not_reached();

  if (!filterx_expr_is_literal(self->lhs))
    return NULL;

  if (!_literal_expr_truthy(self->lhs))
    return filterx_literal_new(filterx_boolean_new(FALSE));

  if (!filterx_expr_is_literal(self->rhs))
    {
      /* we can't optimize the AND away, but the LHS is a literal & truthy
       * value.  In this case we won't need to evaluate this every time in
       * eval(), so unset it */
      filterx_expr_unref(self->lhs);
      self->lhs = NULL;
      return NULL;
    }

  return filterx_literal_new(filterx_boolean_new(_literal_expr_truthy(self->rhs)));
}

static FilterXObject *
_eval_and(FilterXExpr *s)
{
  FilterXBinaryOp *self = (FilterXBinaryOp *) s;

  /* we only evaluate our rhs if lhs is true, e.g.  we do fast circuit
   * evaluation just like most languages */

  if (self->lhs)
    {
      FilterXObject *result = filterx_expr_eval(self->lhs);
      if (!result)
        {
          filterx_eval_push_error_static_info("Failed to evaluate logical AND operation", s,
                                              "Failed to evaluate left hand side");
          return NULL;
        }

      gboolean lhs_truthy = filterx_object_truthy(result);
      filterx_object_unref(result);

      if (!lhs_truthy)
        return filterx_boolean_new(FALSE);
    }

  FilterXObject *result = filterx_expr_eval(self->rhs);
  if (!result)
    {
      filterx_eval_push_error_static_info("Failed to evaluate logical AND operation", s,
                                          "Failed to evaluate right hand side");
      return NULL;
    }

  gboolean rhs_truthy = filterx_object_truthy(result);
  filterx_object_unref(result);

  if (!rhs_truthy)
    return filterx_boolean_new(FALSE);

  return filterx_boolean_new(TRUE);
}

FilterXExpr *
filterx_binary_and_new(FilterXExpr *lhs, FilterXExpr *rhs)
{
  FilterXBinaryOp *self = g_new0(FilterXBinaryOp, 1);

  filterx_binary_op_init_instance(self, "and", FALSE, lhs, rhs);

  self->super.optimize = _optimize_and;
  self->super.eval = _eval_and;
  return &self->super;

}

static FilterXExpr *
_optimize_or(FilterXExpr *s)
{
  FilterXBinaryOp *self = (FilterXBinaryOp *) s;

  if (filterx_binary_op_optimize_method(s))
    g_assert_not_reached();

  if (!filterx_expr_is_literal(self->lhs))
    return NULL;

  if (_literal_expr_truthy(self->lhs))
    return filterx_literal_new(filterx_boolean_new(TRUE));

  if (!filterx_expr_is_literal(self->rhs))
    {
      /* we can't optimize the OR away (as rhs is not literal), but we do
       * know that the lhs is literal and is a falsy value.  In this case we
       * won't need to evaluate this every time in eval(), so unset it */

      filterx_expr_unref(self->lhs);
      self->lhs = NULL;
      return NULL;
    }

  return filterx_literal_new(filterx_boolean_new(_literal_expr_truthy(self->rhs)));
}

static FilterXObject *
_eval_or(FilterXExpr *s)
{
  FilterXBinaryOp *self = (FilterXBinaryOp *) s;

  /* we only evaluate our rhs if lhs is false, e.g.  we do fast circuit
   * evaluation just like most languages */

  if (self->lhs)
    {
      FilterXObject *result = filterx_expr_eval(self->lhs);
      if (!result)
        {
          filterx_eval_push_error_static_info("Failed to evaluate logical OR operation", s,
                                              "Failed to evaluate left hand side");
          return NULL;
        }

      gboolean lhs_truthy = filterx_object_truthy(result);
      filterx_object_unref(result);

      if (lhs_truthy)
        return filterx_boolean_new(TRUE);
    }

  FilterXObject *result = filterx_expr_eval(self->rhs);
  if (!result)
    {
      filterx_eval_push_error_static_info("Failed to evaluate logical OR operation", s,
                                          "Failed to evaluate right hand side");
      return NULL;
    }

  gboolean rhs_truthy = filterx_object_truthy(result);
  filterx_object_unref(result);

  if (rhs_truthy)
    return filterx_boolean_new(TRUE);

  return filterx_boolean_new(FALSE);
}

FilterXExpr *
filterx_binary_or_new(FilterXExpr *lhs, FilterXExpr *rhs)
{
  FilterXBinaryOp *self = g_new0(FilterXBinaryOp, 1);

  filterx_binary_op_init_instance(self, "or", FALSE, lhs, rhs);

  self->super.optimize = _optimize_or;
  self->super.eval = _eval_or;
  return &self->super;
}
