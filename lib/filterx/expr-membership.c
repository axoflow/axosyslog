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
#include "filterx/expr-literal.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-object.h"
#include "expr-comparison.h"

typedef struct FilterXOperatorIn_
{
  FilterXBinaryOp super;
  FilterXObject *literal_lhs;
  FilterXObject *literal_rhs;
} FilterXOperatorIn;

static FilterXObject *
_eval_in(FilterXExpr *s)
{
  FilterXOperatorIn *self = (FilterXOperatorIn *) s;

  FilterXObject *lhs_obj = self->literal_lhs ? filterx_object_ref(self->literal_lhs)
                           : filterx_expr_eval_typed(self->super.lhs);

  if (!lhs_obj)
    {
      filterx_eval_push_error_info_printf("Failed to evaluate 'in' operator", &self->super.super,
                                          "Failed to evaluate left hand side");
      return NULL;
    }

  FilterXObject *lhs = filterx_ref_unwrap_ro(lhs_obj);

  FilterXObject *rhs_obj = self->literal_rhs ? filterx_object_ref(self->literal_rhs)
                           : filterx_expr_eval(self->super.rhs);

  if (!rhs_obj)
    {
      filterx_eval_push_error_info_printf("Failed to evaluate 'in' operator", &self->super.super,
                                          "Failed to evaluate right hand side");
      goto error;
    }

  FilterXObject *list_obj = filterx_ref_unwrap_ro(rhs_obj);

  guint64 size;
  if (!filterx_object_len(list_obj, &size))
    {
      filterx_eval_push_error_static_info("Failed to evaluate 'in' operator", s, "len() method failed");
      goto error;
    }

  for (guint64 i = 0; i < size; i++)
    {
      FilterXObject *elt = filterx_sequence_get_subscript(list_obj, i);

      if (filterx_compare_objects(lhs, elt, FCMPX_TYPE_AND_VALUE_BASED | FCMPX_EQ))
        {
          filterx_object_unref(elt);
          filterx_object_unref(rhs_obj);
          filterx_object_unref(lhs_obj);
          return filterx_boolean_new(TRUE);
        }
      filterx_object_unref(elt);
    }

  filterx_object_unref(rhs_obj);
  filterx_object_unref(lhs_obj);
  return filterx_boolean_new(FALSE);

error:
  filterx_object_unref(rhs_obj);
  filterx_object_unref(lhs_obj);
  return NULL;
}

static FilterXExpr *
_optimize_in(FilterXExpr *s)
{
  FilterXOperatorIn *self = (FilterXOperatorIn *) s;

  if (filterx_binary_op_optimize_method(s))
    g_assert_not_reached();

  if (filterx_expr_is_literal(self->super.lhs))
    self->literal_lhs = filterx_literal_get_value(self->super.lhs);

  if (filterx_expr_is_literal(self->super.rhs))
    self->literal_rhs = filterx_literal_get_value(self->super.rhs);

  if (self->literal_lhs && self->literal_rhs)
    return filterx_literal_new(_eval_in(&self->super.super));
  return NULL;
}

static void
_free_in(FilterXExpr *s)
{
  FilterXOperatorIn *self = (FilterXOperatorIn *) s;
  filterx_object_unref(self->literal_lhs);
  filterx_object_unref(self->literal_rhs);

  filterx_binary_op_free_method(&self->super.super);
}

FilterXExpr *
filterx_membership_in_new(FilterXExpr *lhs, FilterXExpr *rhs)
{
  FilterXOperatorIn *self = g_new0(FilterXOperatorIn, 1);

  filterx_binary_op_init_instance(&self->super, "in", lhs, rhs);
  self->super.super.optimize = _optimize_in;
  self->super.super.eval = _eval_in;
  self->super.super.free_fn = _free_in;

  return &self->super.super;
}
