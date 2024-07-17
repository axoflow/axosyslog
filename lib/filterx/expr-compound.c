/*
 * Copyright (c) 2024 Attila Szakacs
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

#include "filterx/expr-compound.h"
#include "filterx/filterx-eval.h"
#include "filterx/object-primitive.h"

typedef struct _FilterXCompoundExpr
{
  FilterXExpr super;
  GList *exprs;
} FilterXCompoundExpr;

static FilterXObject *
_eval(FilterXExpr *s)
{
  FilterXCompoundExpr *self = (FilterXCompoundExpr *) s;
  FilterXObject *result = NULL;

  if (!filterx_expr_list_eval(self->exprs, &result))
    {
      if (result)
        {
          filterx_eval_push_error("bailing out due to a falsy expr", s, result);
          filterx_object_unref(result);
          result = NULL;
        }
    }
  else
    {
      if (!result)
        {
          /* result NULL means an empty block, implicitly TRUE */
          result = filterx_boolean_new(TRUE);
        }
    }

  return result;
}

static void
_free(FilterXExpr *s)
{
  FilterXCompoundExpr *self = (FilterXCompoundExpr *) s;

  g_list_free_full(self->exprs, (GDestroyNotify) filterx_expr_unref);
  filterx_expr_free_method(s);
}

/* Takes reference of expr */
void
filterx_compound_expr_add(FilterXExpr *s, FilterXExpr *expr)
{
  FilterXCompoundExpr *self = (FilterXCompoundExpr *) s;

  self->exprs = g_list_append(self->exprs, expr);
}

void
filterx_compound_expr_add_list(FilterXExpr *s, GList *expr_list)
{
  FilterXCompoundExpr *self = (FilterXCompoundExpr *) s;

  self->exprs = g_list_concat(self->exprs, expr_list);
}

FilterXExpr *
filterx_compound_expr_new(void)
{
  FilterXCompoundExpr *self = g_new0(FilterXCompoundExpr, 1);

  filterx_expr_init_instance(&self->super);
  self->super.eval = _eval;
  self->super.free_fn = _free;

  return &self->super;
}
