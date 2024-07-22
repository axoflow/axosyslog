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

#include "filterx/func-flatten.h"

typedef struct FilterXFunctionFlatten_
{
  FilterXFunction super;
} FilterXFunctionFlatten;

static FilterXObject *
_eval(FilterXExpr *s)
{
  // TODO: implement
  return NULL;
}

static void
_free(FilterXExpr *s)
{
  FilterXFunctionFlatten *self = (FilterXFunctionFlatten *) s;
  filterx_function_free_method(&self->super);
}

FilterXFunction *
filterx_function_flatten_new(const gchar *function_name, FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionFlatten *self = g_new0(FilterXFunctionFlatten, 1);
  filterx_function_init_instance(&self->super, function_name);
  self->super.super.eval = _eval;
  self->super.super.free_fn = _free;

  filterx_function_args_free(args);
  return &self->super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}
