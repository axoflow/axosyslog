/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 shifter
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

#include "filterx/func-keys.h"
#include "filterx/filterx-mapping.h"
#include "filterx/filterx-sequence.h"
#include "filterx/object-primitive.h"
#include "filterx/filterx-eval.h"
#include "filterx/object-list.h"

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

  FilterXObject *result = filterx_list_new();
  if (!filterx_mapping_keys(object, &result))
    {
      filterx_eval_push_error(FILTERX_FUNC_KEYS_ERR_NONDICT FILTERX_FUNC_KEYS_USAGE, s, NULL);
      filterx_object_unref(result);
      return NULL;
    }
  return result;
}

static void
_free(FilterXExpr *s)
{
  FilterXFunctionKeys *self = (FilterXFunctionKeys *) s;
  filterx_expr_unref(self->object_expr);
  filterx_function_free_method(&self->super);
}

static gboolean
_keys_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXFunctionKeys *self = (FilterXFunctionKeys *) s;

  FilterXExpr **exprs[] = { &self->object_expr };

  for (gsize i = 0; i < G_N_ELEMENTS(exprs); i++)
    {
      if (!filterx_expr_visit(exprs[i], f, user_data))
        return FALSE;
    }

  return TRUE;
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
  self->super.super.walk_children = _keys_walk;
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
