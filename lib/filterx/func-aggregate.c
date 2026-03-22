/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#include "filterx/func-aggregate.h"
#include "filterx/filterx-mapping.h"
#include "filterx/object-primitive.h"
#include "filterx/object-string.h"
#include "filterx/object-extractor.h"
#include "filterx/filterx-eval.h"
#include "scratch-buffers.h"

#define FILTERX_FUNC_AGGREGATE_USAGE "Usage: aggregate(key=dict, values=dict)"

typedef struct FilterXFunctionAggregate_
{
  FilterXFunction super;
  FilterXExpr *key_expr;
  FilterXExpr *values_expr;
} FilterXFunctionAggregate;

static gboolean
_aggregate(FilterXFunctionAggregate *self, FilterXObject *key, FilterXObject *values)
{
  return TRUE;
}

static FilterXObject *
_eval_fx_aggregate(FilterXExpr *s)
{
  FilterXFunctionAggregate *self = (FilterXFunctionAggregate *) s;
  FilterXObject *values = NULL;

  FilterXObject *key = filterx_expr_eval_typed(self->key_expr);
  if (!key)
    goto exit;

  values = filterx_expr_eval_typed(self->values_expr);
  if (!values)
    goto exit;

  gboolean result = _aggregate(self, key, values);

exit:
  filterx_object_unref(key);
  filterx_object_unref(values);
  return result ? filterx_boolean_new(TRUE) : NULL;
}

static void
_free(FilterXExpr *s)
{
  FilterXFunctionAggregate *self = (FilterXFunctionAggregate *) s;

  filterx_expr_unref(self->key_expr);
  filterx_expr_unref(self->values_expr);
  filterx_function_free_method(&self->super);
}

static gboolean
_aggregate_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXFunctionAggregate *self = (FilterXFunctionAggregate *) s;

  FilterXExpr **exprs[] = { &self->key_expr, &self->values_expr };

  for (gsize i = 0; i < G_N_ELEMENTS(exprs); i++)
    {
      if (!filterx_expr_visit(s, exprs[i], f, user_data))
        return FALSE;
    }

  return TRUE;
}

static gboolean
_extract_args(FilterXFunctionAggregate *self, FilterXFunctionArgs *args, GError **error)
{
  if (filterx_function_args_len(args) != 0)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. " FILTERX_FUNC_AGGREGATE_USAGE);
      return FALSE;
    }

  self->key_expr = filterx_function_args_get_named_expr(args, "key");
  self->values_expr = filterx_function_args_get_named_expr(args, "values");
  return TRUE;
}

FilterXExpr *
filterx_function_aggregate_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionAggregate *self = g_new0(FilterXFunctionAggregate, 1);

  filterx_function_init_instance(&self->super, "aggregate", FXE_READ);
  self->super.super.eval = _eval_fx_aggregate;
  self->super.super.walk_children = _aggregate_walk;
  self->super.super.free_fn = _free;

  if (!_extract_args(self, args, error) || !filterx_function_args_check(args, error))
    goto error;

  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}
