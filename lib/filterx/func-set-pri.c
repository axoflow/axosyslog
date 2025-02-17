/*
 * Copyright (c) 2025 Axoflow
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

#include "func-set-pri.h"

typedef struct FilterXFunctionSetPri_
{
  FilterXFunction super;
} FilterXFunctionSetPri;

static FilterXObject *
_set_pri_eval(FilterXExpr *s)
{
  return NULL;
}

static void
_set_pri_free(FilterXExpr *s)
{
  FilterXFunctionSetPri *self = (FilterXFunctionSetPri *) s;

  filterx_function_free_method(&self->super);
}

static gboolean
_extract_set_pri_arg(FilterXFunctionSetPri *self, FilterXFunctionArgs *args, GError **error)
{
  return TRUE;
}

/* Takes reference of args */
FilterXExpr *
filterx_function_set_pri_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionSetPri *self = g_new0(FilterXFunctionSetPri, 1);
  filterx_function_init_instance(&self->super, "set_pri");

  self->super.super.eval = _set_pri_eval;
  self->super.super.free_fn = _set_pri_free;

  if (!_extract_set_pri_arg(self, args, error) ||
      !filterx_function_args_check(args, error))
    goto error;

  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}
