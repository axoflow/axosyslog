/*
 * Copyright (c) 2024 Balazs Scheidler <balazs.scheidler@axoflow.com>
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
#include "expr-plus-assign.h"
#include "filterx-eval.h"

typedef struct FilterXOperatorPlusAssign
{
  FilterXBinaryOp super;
} FilterXOperatorPlusAssign;

static FilterXObject *
_eval_plus_assign(FilterXExpr *s)
{
  FilterXOperatorPlusAssign *self = (FilterXOperatorPlusAssign *) s;

  FilterXObject *rhs_object = filterx_expr_eval_typed(self->super.rhs);
  if (!rhs_object)
    {
      filterx_eval_push_error_static_info("Failed to add values in place", s, "Failed to evaluate right hand side");
      return NULL;
    }

  FilterXObject *res = filterx_expr_plus_assign(self->super.lhs, rhs_object);
  filterx_object_unref(rhs_object);
  if (!res)
    {
      filterx_eval_push_error_static_info("Failed to add values in place", s, "plus_assign() method failed");
      return NULL;
    }

  return res;
}

FilterXExpr *
filterx_operator_plus_assign_new(FilterXExpr *lhs, FilterXExpr *rhs)
{
  FilterXOperatorPlusAssign *self = g_new0(FilterXOperatorPlusAssign, 1);
  filterx_binary_op_init_instance(&self->super, "plus-assign", lhs, rhs);
  self->super.super.eval = _eval_plus_assign;

  return &self->super.super;
}
