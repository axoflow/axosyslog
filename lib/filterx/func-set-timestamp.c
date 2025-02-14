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

#include "func-set-timestamp.h"

typedef struct FilterXFunctionSetTimestamp_
{
  FilterXFunction super;
} FilterXFunctionSetTimestamp;

static FilterXObject *
_set_timestamp_eval(FilterXExpr *s)
{
  return NULL;
}

static void
_set_timestamp_free(FilterXExpr *s)
{
  FilterXFunctionSetTimestamp *self = (FilterXFunctionSetTimestamp *) s;

  filterx_function_free_method(&self->super);
}


static gboolean
_extract_set_timestamp_args(FilterXFunctionSetTimestamp *self, FilterXFunctionArgs *args, GError **error)
{
  return TRUE;
}

/* Takes reference of args */
FilterXExpr *
filterx_function_set_timestamp_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionSetTimestamp *self = g_new0(FilterXFunctionSetTimestamp, 1);
  filterx_function_init_instance(&self->super, "set_timestamp");

  self->super.super.eval = _set_timestamp_eval;
  self->super.super.free_fn = _set_timestamp_free;

  if (!_extract_set_timestamp_args(self, args, error) ||
      !filterx_function_args_check(args, error))
    goto error;

  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}
