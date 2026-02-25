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
#include "filterx/filterx-mapping.h"
#include "filterx/expr-literal.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-object.h"
#include "expr-comparison.h"

typedef struct FilterXOperatorIn_
{
  FilterXBinaryOp super;
} FilterXOperatorIn;

static FilterXObject *
_eval_in_dict(FilterXOperatorIn *self, FilterXObject *dict_obj, FilterXObject *needle_obj)
{
  gboolean is_key_set = filterx_object_is_key_set(dict_obj, needle_obj);
  return filterx_boolean_new(is_key_set);
}

static FilterXObject *
_eval_in_list(FilterXOperatorIn *self, FilterXObject *list_obj, FilterXObject *needle_obj)
{
  guint64 size;

  if (!filterx_object_len(list_obj, &size))
    {
      filterx_eval_push_error_static_info("Failed to evaluate 'in' operator", &self->super.super, "len() method failed");
      return NULL;
    }

  FilterXObject *needle = filterx_ref_unwrap_ro(needle_obj);
  for (guint64 i = 0; i < size; i++)
    {
      FilterXObject *elt = filterx_sequence_get_subscript(list_obj, i);

      if (filterx_compare_objects(needle, elt, FCMPX_TYPE_AND_VALUE_BASED | FCMPX_EQ))
        {
          filterx_object_unref(elt);
          return filterx_boolean_new(TRUE);
        }
      filterx_object_unref(elt);
    }

  return filterx_boolean_new(FALSE);
}

FilterXObject *
_eval_in_value(FilterXOperatorIn *self, FilterXObject *lhs_obj, FilterXObject *rhs_obj)
{
  FilterXObject *iterable_obj = filterx_ref_unwrap_ro(rhs_obj);

  if (filterx_object_is_type(iterable_obj, &FILTERX_TYPE_NAME(mapping)))
    return _eval_in_dict(self, iterable_obj, lhs_obj);
  else if (filterx_object_is_type(iterable_obj, &FILTERX_TYPE_NAME(sequence)))
    return _eval_in_list(self, iterable_obj, lhs_obj);
  else
    {
      filterx_eval_push_error_info_printf("Failed to evaluate 'in' operator", &self->super.super,
                                          "Right hand side must be list or dict type, got: %s",
                                          filterx_object_get_type_name(iterable_obj));
      return NULL;
    }
}

static FilterXObject *
_eval_in(FilterXExpr *s)
{
  FilterXOperatorIn *self = (FilterXOperatorIn *) s;
  FilterXObject *result = NULL;

  FilterXObject *lhs_obj = filterx_expr_eval_typed(self->super.lhs);
  if (!lhs_obj)
    {
      filterx_eval_push_error_info_printf("Failed to evaluate 'in' operator", &self->super.super,
                                          "Failed to evaluate left hand side");
      return NULL;
    }


  FilterXObject *rhs_obj = filterx_expr_eval(self->super.rhs);
  if (!rhs_obj)
    {
      filterx_eval_push_error_info_printf("Failed to evaluate 'in' operator", &self->super.super,
                                          "Failed to evaluate right hand side");
      goto exit;
    }

  result = _eval_in_value(self, lhs_obj, rhs_obj);
exit:
  filterx_object_unref(rhs_obj);
  filterx_object_unref(lhs_obj);
  return result;
}

static FilterXExpr *
_optimize_in(FilterXExpr *s)
{
  FilterXOperatorIn *self = (FilterXOperatorIn *) s;
  FilterXObject *lhs_obj = NULL, *rhs_obj = NULL;
  FilterXExpr *result = NULL;

  if (filterx_expr_is_literal(self->super.lhs))
    lhs_obj = filterx_literal_get_value(self->super.lhs);

  if (filterx_expr_is_literal(self->super.rhs))
    rhs_obj = filterx_literal_get_value(self->super.rhs);

  if (lhs_obj && rhs_obj)
    result = filterx_literal_new(_eval_in_value(self, lhs_obj, rhs_obj));

  filterx_object_unref(lhs_obj);
  filterx_object_unref(rhs_obj);
  return result;
}

static void
_free_in(FilterXExpr *s)
{
  FilterXOperatorIn *self = (FilterXOperatorIn *) s;

  filterx_binary_op_free_method(&self->super.super);
}

FilterXExpr *
filterx_membership_in_new(FilterXExpr *lhs, FilterXExpr *rhs)
{
  FilterXOperatorIn *self = g_new0(FilterXOperatorIn, 1);

  filterx_binary_op_init_instance(&self->super, "in", FXE_READ, lhs, rhs);
  self->super.super.optimize = _optimize_in;
  self->super.super.eval = _eval_in;
  self->super.super.free_fn = _free_in;

  return &self->super.super;
}
