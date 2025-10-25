/*
 * Copyright (c) 2025 Axoflow
 * Copyright (c) 2025 Tamas Kosztyu <tamas.kosztyu@axoflow.com>
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

#include "func-set-pri.h"
#include "filterx/object-extractor.h"
#include "filterx/object-primitive.h"
#include "filterx/filterx-eval.h"

#define FILTERX_FUNC_SET_PRI_USAGE "Usage: set_pri([value])"

typedef struct FilterXFunctionSetPri_
{
  FilterXFunction super;

  FilterXExpr *pri_expr;
} FilterXFunctionSetPri;

static FilterXObject *
_set_pri_eval(FilterXExpr *s)
{
  FilterXFunctionSetPri *self = (FilterXFunctionSetPri *) s;

  FilterXObject *pri_obj = filterx_expr_eval(self->pri_expr);

  gint64 pri_val;

  if (!filterx_object_extract_integer(pri_obj, &pri_val))
    {
      filterx_object_unref(pri_obj);
      filterx_eval_push_error("Pri must be of integer type. " FILTERX_FUNC_SET_PRI_USAGE, s, NULL);
      return NULL;
    }

  if (pri_val > 191 || pri_val < 0)
    {
      filterx_object_unref(pri_obj);
      filterx_eval_push_error("Failed to set pri, value must be between 0 and 191 inclusive. " FILTERX_FUNC_SET_PRI_USAGE, s,
                              NULL);
      return NULL;
    }

  filterx_object_unref(pri_obj);

  FilterXEvalContext *context = filterx_eval_get_context();
  LogMessage *msg = context->msg;

  msg->pri = pri_val;

  return filterx_boolean_new(TRUE);
}

static FilterXExpr *
_set_pri_optimize(FilterXExpr *s)
{
  FilterXFunctionSetPri *self = (FilterXFunctionSetPri *) s;

  self->pri_expr = filterx_expr_optimize(self->pri_expr);
  return filterx_function_optimize_method(&self->super);
}

static gboolean
_set_pri_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionSetPri *self = (FilterXFunctionSetPri *) s;

  if (!filterx_expr_init(self->pri_expr, cfg))
    return FALSE;

  return filterx_function_init_method(&self->super, cfg);
}

static void
_set_pri_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionSetPri *self = (FilterXFunctionSetPri *) s;

  filterx_expr_deinit(self->pri_expr, cfg);
  filterx_function_deinit_method(&self->super, cfg);
}

static void
_set_pri_free(FilterXExpr *s)
{
  FilterXFunctionSetPri *self = (FilterXFunctionSetPri *) s;

  filterx_function_free_method(&self->super);
}

static gboolean
_set_pri_walk(FilterXExpr *s, FilterXExprWalkOrder order, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXFunctionSetPri *self = (FilterXFunctionSetPri *) s;

  FilterXExpr *exprs[] = { self->pri_expr, NULL };

  for (gsize i = 0; i < G_N_ELEMENTS(exprs); i++)
    {
      if (!filterx_expr_walk(exprs[i], order, f, user_data))
        return FALSE;
    }

  return TRUE;
}

static gboolean
_extract_set_pri_arg(FilterXFunctionSetPri *self, FilterXFunctionArgs *args, GError **error)
{
  self->pri_expr = filterx_function_args_get_expr(args, 0);

  if (!self->pri_expr)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "argument must be set: pri. " FILTERX_FUNC_SET_PRI_USAGE);
      return FALSE;
    }

  return TRUE;
}

/* Takes reference of args */
FilterXExpr *
filterx_function_set_pri_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionSetPri *self = g_new0(FilterXFunctionSetPri, 1);
  filterx_function_init_instance(&self->super, "set_pri");

  self->super.super.eval = _set_pri_eval;
  self->super.super.optimize = _set_pri_optimize;
  self->super.super.init = _set_pri_init;
  self->super.super.deinit = _set_pri_deinit;
  self->super.super.walk_children = _set_pri_walk;
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
