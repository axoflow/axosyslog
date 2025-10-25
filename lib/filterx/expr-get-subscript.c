/*
 * Copyright (c) 2023 Balazs Scheidler <balazs.scheidler@axoflow.com>
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
#include "filterx/expr-get-subscript.h"
#include "filterx/filterx-eval.h"
#include "stats/stats-registry.h"
#include "stats/stats-cluster-single.h"

typedef struct _FilterXGetSubscript
{
  FilterXExpr super;
  FilterXExpr *operand;
  FilterXExpr *key;
} FilterXGetSubscript;

static FilterXObject *
_eval_get_subscript(FilterXExpr *s)
{
  FilterXGetSubscript *self = (FilterXGetSubscript *) s;
  FilterXObject *result = NULL;

  FilterXObject *variable = filterx_expr_eval_typed(self->operand);
  if (!variable)
    {
      filterx_eval_push_error_static_info("Failed to get-subscript from object", s, "Failed to evaluate expression");
      return NULL;
    }

  FilterXObject *key = filterx_expr_eval_typed(self->key);
  if (!key)
    {
      filterx_eval_push_error_static_info("Failed to get-subscript from object", s, "Failed to evaluate key");
      goto exit;
    }

  result = filterx_object_get_subscript(variable, key);
  if (!result)
    filterx_eval_push_error("Failed to get-subscript from object", s, key);

exit:
  filterx_object_unref(key);
  filterx_object_unref(variable);
  return result;
}

static gboolean
_isset(FilterXExpr *s)
{
  FilterXGetSubscript *self = (FilterXGetSubscript *) s;
  FilterXObject *variable = filterx_expr_eval_typed(self->operand);
  if (!variable)
    {
      filterx_eval_push_error_static_info("Failed to check element of object", s, "Failed to evaluate expression");
      return FALSE;
    }

  FilterXObject *key = filterx_expr_eval_typed(self->key);
  if (!key)
    {
      filterx_eval_push_error_static_info("Failed to check element of object", s, "Failed to evaluate key");
      filterx_object_unref(variable);
      return FALSE;
    }

  gboolean result = filterx_object_is_key_set(variable, key);

  filterx_object_unref(key);
  filterx_object_unref(variable);
  return result;
}

static gboolean
_unset(FilterXExpr *s)
{
  FilterXGetSubscript *self = (FilterXGetSubscript *) s;

  gboolean result = FALSE;

  FilterXObject *variable = filterx_expr_eval_typed(self->operand);
  if (!variable)
    {
      filterx_eval_push_error_static_info("Failed to unset from object", s, "Failed to evaluate expression");
      return FALSE;
    }

  FilterXObject *key = filterx_expr_eval_typed(self->key);
  if (!key)
    {
      filterx_eval_push_error_static_info("Failed to unset from object", s, "Failed to evaluate key");
      goto exit;
    }

  if (variable->readonly)
    {
      filterx_eval_push_error_static_info("Failed to unset from object", s, "Object is readonly");
      goto exit;
    }

  result = filterx_object_unset_key(variable, key);

exit:
  filterx_object_unref(key);
  filterx_object_unref(variable);
  return result;
}

static FilterXExpr *
_optimize(FilterXExpr *s)
{
  FilterXGetSubscript *self = (FilterXGetSubscript *) s;

  self->operand = filterx_expr_optimize(self->operand);
  self->key = filterx_expr_optimize(self->key);
  return NULL;
}

static gboolean
_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXGetSubscript *self = (FilterXGetSubscript *) s;

  if (!filterx_expr_init(self->operand, cfg))
    return FALSE;

  if (!filterx_expr_init(self->key, cfg))
    {
      filterx_expr_deinit(self->operand, cfg);
      return FALSE;
    }

  return filterx_expr_init_method(s, cfg);
}

static void
_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXGetSubscript *self = (FilterXGetSubscript *) s;

  filterx_expr_deinit(self->operand, cfg);
  filterx_expr_deinit(self->key, cfg);
  filterx_expr_deinit_method(s, cfg);
}

static void
_free(FilterXExpr *s)
{
  FilterXGetSubscript *self = (FilterXGetSubscript *) s;
  filterx_expr_unref(self->key);
  filterx_expr_unref(self->operand);
  filterx_expr_free_method(s);
}

static gboolean
_get_subscript_walk(FilterXExpr *s, FilterXExprWalkOrder order, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXGetSubscript *self = (FilterXGetSubscript *) s;

  FilterXExpr *exprs[] = { self->operand, self->key, NULL };

  for (gsize i = 0; i < G_N_ELEMENTS(exprs); i++)
    {
      if (!filterx_expr_walk(exprs[i], order, f, user_data))
        return FALSE;
    }

  return TRUE;
}

/* NOTE: takes the object reference */
FilterXExpr *
filterx_get_subscript_new(FilterXExpr *operand, FilterXExpr *key)
{
  FilterXGetSubscript *self = g_new0(FilterXGetSubscript, 1);

  filterx_expr_init_instance(&self->super, FILTERX_EXPR_TYPE_NAME(get_subscript));
  self->super.eval = _eval_get_subscript;
  self->super.is_set = _isset;
  self->super.unset = _unset;
  self->super.optimize = _optimize;
  self->super.init = _init;
  self->super.deinit = _deinit;
  self->super.walk_children = _get_subscript_walk;
  self->super.free_fn = _free;
  self->operand = operand;
  self->key = key;
  return &self->super;
}

FILTERX_EXPR_DEFINE_TYPE(get_subscript);
