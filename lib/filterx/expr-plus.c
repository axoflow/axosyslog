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
#include "expr-plus.h"
#include "object-string.h"
#include "filterx-eval.h"
#include "filterx/expr-literal.h"
#include "scratch-buffers.h"

typedef struct FilterXOperatorPlus
{
  FilterXBinaryOp super;
  FilterXObject *literal_lhs;
  FilterXObject *literal_rhs;
} FilterXOperatorPlus;

static FilterXObject *
_do_plus(FilterXObject *lhs, FilterXObject *rhs, FilterXExpr *expr)
{
  FilterXObject *result = NULL;

  if (!lhs)
    {
      filterx_eval_push_error_static_info("Failed to add values", expr, "Failed to evaluate left hand side");
      goto exit;
    }
  if (!rhs)
    {
      filterx_eval_push_error_static_info("Failed to add values", expr, "Failed to evaluate right hand side");
      goto exit;
    }

  result = filterx_object_add(lhs, rhs);
  if (!result)
    filterx_eval_push_error_static_info("Failed to add values", expr, "add() method failed");

exit:
  filterx_object_unref(lhs);
  filterx_object_unref(rhs);
  return result;
}

static FilterXObject *
_eval_plus(FilterXExpr *s)
{
  FilterXOperatorPlus *self = (FilterXOperatorPlus *) s;
  FilterXObject *lhs = self->literal_lhs ? filterx_object_ref(self->literal_lhs)
                       : filterx_expr_eval_typed(self->super.lhs);
  FilterXObject *rhs = self->literal_rhs ? filterx_object_ref(self->literal_rhs)
                       : filterx_expr_eval(self->super.rhs);
  return _do_plus(lhs, rhs, s);
}

static FilterXExpr *
_optimize(FilterXExpr *s)
{
  FilterXOperatorPlus *self = (FilterXOperatorPlus *) s;

  if (filterx_expr_is_literal(self->super.lhs))
    self->literal_lhs = filterx_literal_get_value(self->super.lhs);

  if (filterx_expr_is_literal(self->super.rhs))
    self->literal_rhs = filterx_literal_get_value(self->super.rhs);

  if (self->literal_lhs && self->literal_rhs)
    return filterx_literal_new(_eval_plus(&self->super.super));
  return NULL;
}

static void
_filterx_operator_plus_free(FilterXExpr *s)
{
  FilterXOperatorPlus *self = (FilterXOperatorPlus *) s;
  filterx_object_unref(self->literal_lhs);
  filterx_object_unref(self->literal_rhs);

  filterx_binary_op_free_method(s);
}

FilterXExpr *
filterx_operator_plus_new(FilterXExpr *lhs, FilterXExpr *rhs)
{
  FilterXOperatorPlus *self = g_new0(FilterXOperatorPlus, 1);
  filterx_binary_op_init_instance(&self->super, "plus", FXE_READ, lhs, rhs);
  self->super.super.optimize = _optimize;
  self->super.super.eval = _eval_plus;
  self->super.super.free_fn = _filterx_operator_plus_free;

  return &self->super.super;
}
