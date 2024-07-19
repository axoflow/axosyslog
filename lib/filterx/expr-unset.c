/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 Attila Szakacs <attila.szakacs@axoflow.com>
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

#include "filterx/expr-unset.h"
#include "filterx/object-primitive.h"

typedef struct FilterXExprUnset_
{
  FilterXFunction super;
  GPtrArray *exprs;
} FilterXExprUnset;

static FilterXObject *
_eval(FilterXExpr *s)
{
  FilterXExprUnset *self = (FilterXExprUnset *) s;

  for (guint i = 0; i < self->exprs->len; i++)
    {
      FilterXExpr *expr = (FilterXExpr *) g_ptr_array_index(self->exprs, i);
      if (!filterx_expr_unset(expr))
        return NULL;
    }

  return filterx_boolean_new(TRUE);
}

static void
_free(FilterXExpr *s)
{
  FilterXExprUnset *self = (FilterXExprUnset *) s;

  g_ptr_array_unref(self->exprs);
  filterx_function_free_method(&self->super);
}

FilterXFunction *
filterx_function_unset_new(const gchar *function_name, FilterXFunctionArgs *args, GError **error)
{
  FilterXExprUnset *self = g_new0(FilterXExprUnset, 1);
  filterx_function_init_instance(&self->super, function_name);

  self->super.super.eval = _eval;
  self->super.super.free_fn = _free;

  self->exprs = g_ptr_array_new_full(filterx_function_args_len(args), (GDestroyNotify) filterx_expr_unref);
  for (guint64 i = 0; i < filterx_function_args_len(args); i++)
    g_ptr_array_add(self->exprs, filterx_function_args_get_expr(args, i));

  if (!filterx_function_args_check(args, error))
    goto error;

  filterx_function_args_free(args);
  return &self->super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}
