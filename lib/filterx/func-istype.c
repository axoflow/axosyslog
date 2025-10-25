/*
 * Copyright (c) 2024 shifter
 * Copyright (c) 2024 Attila Szakacs
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

#include "filterx/func-istype.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "filterx/expr-literal.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-globals.h"

#define FILTERX_FUNC_ISTYPE_USAGE "Usage: istype(object, type_str)"

typedef struct FilterXFunctionIsType_
{
  FilterXFunction super;
  FilterXExpr *object_expr;
  FilterXType *type;
} FilterXFunctionIsType;

static gboolean
_object_istype(FilterXFunctionIsType *self, FilterXObject *object)
{
  FilterXObject *unwrapped = filterx_ref_unwrap_ro(object);
  return filterx_object_is_type(unwrapped, self->type);
}

static FilterXObject *
_eval_fx_istype(FilterXExpr *s)
{
  FilterXFunctionIsType *self = (FilterXFunctionIsType *) s;

  FilterXObject *object = filterx_expr_eval(self->object_expr);
  if (!object)
    {
      filterx_eval_push_error("Failed to evaluate first argument. " FILTERX_FUNC_ISTYPE_USAGE, s, NULL);
      return NULL;
    }

  gboolean result = _object_istype(self, object);
  filterx_object_unref(object);
  return filterx_boolean_new(result);
}

static FilterXExpr *
_optimize(FilterXExpr *s)
{
  FilterXFunctionIsType *self = (FilterXFunctionIsType *) s;

  self->object_expr = filterx_expr_optimize(self->object_expr);

  if (filterx_expr_is_literal(self->object_expr))
    {
      FilterXObject *object = filterx_literal_get_value(self->object_expr);
      gboolean result = _object_istype(self, object);
      filterx_object_unref(object);

      return filterx_literal_new(filterx_boolean_new(result));
    }

  return filterx_function_optimize_method(&self->super);
}

static gboolean
_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionIsType *self = (FilterXFunctionIsType *) s;

  if (!filterx_expr_init(self->object_expr, cfg))
    return FALSE;

  return filterx_function_init_method(&self->super, cfg);
}

static void
_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionIsType *self = (FilterXFunctionIsType *) s;
  filterx_expr_deinit(self->object_expr, cfg);
  filterx_function_deinit_method(&self->super, cfg);
}

static void
_free(FilterXExpr *s)
{
  FilterXFunctionIsType *self = (FilterXFunctionIsType *) s;
  filterx_expr_unref(self->object_expr);
  filterx_function_free_method(&self->super);
}

static gboolean
_istype_walk(FilterXExpr *s, FilterXExprWalkOrder order, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXFunctionIsType *self = (FilterXFunctionIsType *) s;

  FilterXExpr *exprs[] = { self->object_expr, NULL };

  for (gsize i = 0; i < G_N_ELEMENTS(exprs); i++)
    {
      if (!filterx_expr_walk(exprs[i], order, f, user_data))
        return FALSE;
    }

  return TRUE;
}

static FilterXType *
_extract_type(FilterXFunctionArgs *args, GError **error)
{
  const gchar *type_str = filterx_function_args_get_literal_string(args, 1, NULL);
  if (!type_str)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "argument must be string literal: type. " FILTERX_FUNC_ISTYPE_USAGE);
      return NULL;
    }

  FilterXType *type = filterx_type_lookup(type_str);
  if (!type)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "unknown type: %s. " FILTERX_FUNC_ISTYPE_USAGE, type_str);
      return NULL;
    }

  return type;
}

static FilterXExpr *
_extract_object_expr(FilterXFunctionArgs *args, GError **error)
{
  FilterXExpr *object_expr = filterx_function_args_get_expr(args, 0);
  if (!object_expr)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "argument must be set: object. " FILTERX_FUNC_ISTYPE_USAGE);
      return NULL;
    }

  return object_expr;
}

static gboolean
_extract_args(FilterXFunctionIsType *self, FilterXFunctionArgs *args, GError **error)
{
  if (filterx_function_args_len(args) != 2)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. " FILTERX_FUNC_ISTYPE_USAGE);
      return FALSE;
    }

  self->object_expr = _extract_object_expr(args, error);
  if (!self->object_expr)
    return FALSE;

  self->type = _extract_type(args, error);
  if (!self->type)
    return FALSE;

  return TRUE;
}

FilterXExpr *
filterx_function_istype_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionIsType *self = g_new0(FilterXFunctionIsType, 1);
  filterx_function_init_instance(&self->super, "istype");
  self->super.super.eval = _eval_fx_istype;
  self->super.super.optimize = _optimize;
  self->super.super.init = _init;
  self->super.super.deinit = _deinit;
  self->super.super.walk_children = _istype_walk;
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
