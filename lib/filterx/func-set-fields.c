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

#include "filterx/func-set-fields.h"

typedef struct FilterXFunctionSetFields_
{
  FilterXFunction super;
} FilterXFunctionSetFields;

static FilterXObject *
_eval(FilterXExpr *s)
{
  // TODO: implement
  return NULL;
}

static gboolean
_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionSetFields *self = (FilterXFunctionSetFields *) s;

  return filterx_function_init_method(&self->super, cfg);
}

static void
_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionSetFields *self = (FilterXFunctionSetFields *) s;

  filterx_function_deinit_method(&self->super, cfg);
}

static void
_free(FilterXExpr *s)
{
  FilterXFunctionSetFields *self = (FilterXFunctionSetFields *) s;

  filterx_function_free_method(&self->super);
}

static gboolean
_extract_args(FilterXFunctionSetFields *self, FilterXFunctionArgs *args, GError **error)
{
  // TODO: implement
  return TRUE;
}

FilterXExpr *
filterx_function_set_fields_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionSetFields *self = g_new0(FilterXFunctionSetFields, 1);
  filterx_function_init_instance(&self->super, "set_fields");

  self->super.super.eval = _eval;
  self->super.super.init = _init;
  self->super.super.deinit = _deinit;
  self->super.super.free_fn = _free;

  if (!_extract_args(self, args, error) ||
      !filterx_function_args_check(args, error))
    goto error;

  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}
