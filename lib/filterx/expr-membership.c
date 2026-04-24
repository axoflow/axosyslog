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
#include "filterx/expr-membership.h"
#include "filterx/object-primitive.h"
#include "filterx/filterx-sequence.h"
#include "filterx/object-dict.h"
#include "filterx/expr-literal.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-object.h"
#include "expr-comparison.h"

typedef struct FilterXOperatorIn_
{
  FilterXBinaryOp super;
} FilterXOperatorIn;

static FilterXObject *
_eval_in(FilterXExpr *s)
{
  FilterXOperatorIn *self = (FilterXOperatorIn *) s;
  FilterXObject *result = NULL;

  FilterXObject *member = filterx_expr_eval_typed(self->super.lhs);

  if (!member)
    {
      filterx_eval_push_error_info_printf("Failed to evaluate 'in' operator", &self->super.super,
                                          "Failed to evaluate member expression");
      return NULL;
    }

  FilterXObject *container = filterx_expr_eval(self->super.rhs);
  if (!container)
    {
      filterx_eval_push_error_info_printf("Failed to evaluate 'in' operator", &self->super.super,
                                          "Failed to evaluate container expression");
      goto exit;
    }

  result = filterx_object_is_member_of(container, member);

exit:
  filterx_object_unref(container);
  filterx_object_unref(member);
  return result;
}

static FilterXExpr *
_optimize_in(FilterXExpr *s)
{
  FilterXOperatorIn *self = (FilterXOperatorIn *) s;
  FilterXExpr *result = NULL;
  FilterXObject *member = NULL;
  FilterXObject *container = NULL;

  if (filterx_expr_is_literal(self->super.lhs))
    member = filterx_literal_get_value(self->super.lhs);

  if (filterx_expr_is_literal(self->super.rhs))
    container = filterx_literal_get_value(self->super.rhs);

  if (member && container)
    result = filterx_literal_new(filterx_object_is_member_of(container, member));
  filterx_object_unref(member);
  filterx_object_unref(container);
  return result;
}

FilterXExpr *
filterx_membership_in_new(FilterXExpr *lhs, FilterXExpr *rhs)
{
  FilterXOperatorIn *self = g_new0(FilterXOperatorIn, 1);

  filterx_binary_op_init_instance(&self->super, "in", FXE_READ, lhs, rhs);
  self->super.super.optimize = _optimize_in;
  self->super.super.eval = _eval_in;

  return &self->super.super;
}
