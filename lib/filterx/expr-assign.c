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
#include "filterx/expr-assign.h"
#include "filterx/object-primitive.h"
#include "filterx/filterx-eval.h"
#include "filterx/object-null.h"
#include "filterx/object-message-value.h"
#include "filterx/object-extractor.h"
#include "scratch-buffers.h"

typedef struct FilterXAssign
{
  FilterXBinaryOp super;
} FilterXAssign;

static inline FilterXObject *
_assign(FilterXAssign *self, FilterXObject *value)
{
  FilterXObject *cloned = filterx_object_cow_fork2(filterx_object_ref(value), NULL);
  if (!filterx_expr_assign(self->super.lhs, &cloned))
    {
      filterx_object_unref(cloned);
      return NULL;
    }

  return cloned;
}

static inline FilterXObject *
_suppress_error(void)
{
  filterx_eval_dump_errors("FilterX: null coalesce assignment suppressing error");

  return filterx_null_new();
}

static FilterXObject *
_nullv_assign_eval(FilterXExpr *s)
{
  FilterXAssign *self = (FilterXAssign *) s;

  FilterXObject *value = filterx_expr_eval(self->super.rhs);

  if (!value || filterx_object_extract_null(value))
    {
      if (!value)
        return _suppress_error();

      return value;
    }

  FilterXObject *result = _assign(self, value);
  filterx_object_unref(value);
  return result;
}

static FilterXObject *
_assign_eval(FilterXExpr *s)
{
  FilterXAssign *self = (FilterXAssign *) s;

  FilterXObject *value = filterx_expr_eval(self->super.rhs);

  if (!value)
    {
      filterx_eval_push_error_static_info("Failed to assign value", s, "Failed to evaluate right hand side");
      return NULL;
    }

  FilterXObject *result = _assign(self, value);
  filterx_object_unref(value);

  if (!result)
    filterx_eval_push_error_static_info("Failed to assign value", s, "assign() method failed");

  return result;
}

static void
filterx_assign_init_instance(FilterXAssign *self, const gchar *type,
                             FilterXExpr *lhs, FilterXExpr *rhs)
{
  filterx_binary_op_init_instance(&self->super, type, lhs, rhs);
  self->super.super.ignore_falsy_result = TRUE;
}

/* NOTE: takes the object reference */
FilterXExpr *
filterx_assign_new(FilterXExpr *lhs, FilterXExpr *rhs)
{
  FilterXAssign *self = g_new0(FilterXAssign, 1);

  filterx_assign_init_instance(self, "assign", lhs, rhs);
  self->super.super.eval = _assign_eval;
  return &self->super.super;
}

FilterXExpr *
filterx_nullv_assign_new(FilterXExpr *lhs, FilterXExpr *rhs)
{
  FilterXAssign *self = g_new0(FilterXAssign, 1);

  filterx_assign_init_instance(self, "nullv-assign", lhs, rhs);
  self->super.super.eval = _nullv_assign_eval;
  return &self->super.super;
}
