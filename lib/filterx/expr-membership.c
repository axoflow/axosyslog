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
#include "filterx/expr-membership.h"
#include "filterx/object-primitive.h"
#include "filterx/object-list-interface.h"
#include "filterx/expr-literal.h"
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
                           : filterx_expr_eval(self->super.lhs);

  FilterXObject *lhs = filterx_ref_unwrap_ro(lhs_obj);

  filterx_object_unref(lhs_obj);

  FilterXObject *rhs_obj = self->literal_rhs ? filterx_object_ref(self->literal_rhs)
                           : filterx_expr_eval(self->super.rhs);
  FilterXObject *list_obj = filterx_ref_unwrap_ro(rhs_obj);

  filterx_object_unref(rhs_obj);
  if (!filterx_object_is_type(list_obj, &FILTERX_TYPE_NAME(list)))
    {
      msg_error("FilterX: in operator right hand side must be list type",
                evt_tag_str("type", list_obj->type->name));
      return NULL;
    }

  guint64 size;

  if (!filterx_object_len(list_obj, &size))
    return NULL;

  for (guint64 i = 0; i < size; i++)
    {
      FilterXObject *elt = filterx_list_get_subscript(list_obj, i);

      if (filterx_compare_objects(lhs, elt, FCMPX_TYPE_AND_VALUE_BASED | FCMPX_EQ))
        {
          filterx_object_unref(elt);
          return filterx_boolean_new(TRUE);
        }
      filterx_object_unref(elt);
    }

  return filterx_boolean_new(FALSE);
}

static FilterXExpr *
_optimize_in(FilterXExpr *s)
{
  FilterXOperatorIn *self = (FilterXOperatorIn *) s;

  if (filterx_binary_op_optimize_method(s))
    g_assert_not_reached();

  if (filterx_expr_is_literal(self->super.lhs))
    self->literal_lhs = filterx_expr_eval_typed(self->super.lhs);

  if (filterx_expr_is_literal(self->super.rhs))
    self->literal_rhs = filterx_expr_eval(self->super.rhs);

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
