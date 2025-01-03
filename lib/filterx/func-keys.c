/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 shifter
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

#include "filterx/func-keys.h"
#include "filterx/object-dict-interface.h"
#include "filterx/object-list-interface.h"
#include "filterx/object-primitive.h"
#include "filterx/filterx-eval.h"
#include "filterx/object-json.h"

typedef struct FilterXFunctionKeys_
{
  FilterXFunction super;
  FilterXExpr *object_expr;
} FilterXFunctionKeys;

static FilterXObject *
_eval(FilterXExpr *s)
{
  FilterXFunctionKeys *self = (FilterXFunctionKeys *) s;

  FilterXObject *object = filterx_expr_eval(self->object_expr);
  if (!object)
    {
      filterx_eval_push_error("Failed to evaluate first argument. " FILTERX_FUNC_KEYS_USAGE, s, NULL);
      return NULL;
    }

  FilterXObject *result = filterx_json_array_new_empty();
  if (!filterx_dict_keys(object, &result))
    {
      filterx_eval_push_error(FILTERX_FUNC_KEYS_ERR_NONDICT FILTERX_FUNC_KEYS_USAGE, s, NULL);
      filterx_object_unref(result);
      return NULL;
    }
  return result;
}

static FilterXExpr *
_optimize(FilterXExpr *s)
{
  FilterXFunctionKeys *self = (FilterXFunctionKeys *) s;

  self->object_expr = filterx_expr_optimize(self->object_expr);
  return filterx_function_optimize_method(&self->super);
}

static gboolean
_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionKeys *self = (FilterXFunctionKeys *) s;

  if (!filterx_expr_init(self->object_expr, cfg))
    return FALSE;

  return filterx_function_init_method(&self->super, cfg);
}

static void
_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionKeys *self = (FilterXFunctionKeys *) s;
  filterx_expr_deinit(self->object_expr, cfg);
  filterx_function_deinit_method(&self->super, cfg);
}

static void
_free(FilterXExpr *s)
{
  FilterXFunctionKeys *self = (FilterXFunctionKeys *) s;
  filterx_expr_unref(self->object_expr);
  filterx_function_free_method(&self->super);
}

static FilterXExpr *
_extract_object_expr(FilterXFunctionArgs *args, GError **error)
{
  FilterXExpr *object_expr = filterx_function_args_get_expr(args, 0);
  if (!object_expr)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "argument must be set: object. " FILTERX_FUNC_KEYS_USAGE);
      return NULL;
    }

  return object_expr;
}

static gboolean
_extract_args(FilterXFunctionKeys *self, FilterXFunctionArgs *args, GError **error)
{
  if (filterx_function_args_len(args) != 1)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  FILTERX_FUNC_KEYS_ERR_NUM_ARGS FILTERX_FUNC_KEYS_USAGE);
      return FALSE;
    }

  self->object_expr = _extract_object_expr(args, error);
  if (!self->object_expr)
    return FALSE;

  return TRUE;
}

FilterXExpr *
filterx_function_keys_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionKeys *self = g_new0(FilterXFunctionKeys, 1);
  filterx_function_init_instance(&self->super, "keys");
  self->super.super.eval = _eval;
  self->super.super.optimize = _optimize;
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
