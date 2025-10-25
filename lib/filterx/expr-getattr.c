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
#include "filterx/expr-getattr.h"
#include "filterx/object-string.h"
#include "filterx/filterx-eval.h"
#include "stats/stats-registry.h"
#include "stats/stats-cluster-single.h"

typedef struct _FilterXGetAttr
{
  FilterXExpr super;
  FilterXExpr *operand;
  FilterXObject *attr;
} FilterXGetAttr;

static FilterXObject *
_eval_getattr(FilterXExpr *s)
{
  FilterXGetAttr *self = (FilterXGetAttr *) s;

  FilterXObject *variable = filterx_expr_eval_typed(self->operand);

  if (!variable)
    {
      filterx_eval_push_error_static_info("Failed to get-attribute from object", s, "Failed to evaluate expression");
      return NULL;
    }

  FilterXObject *attr = filterx_object_getattr(variable, self->attr);
  if (!attr)
    {
      filterx_eval_push_error_static_info("Failed to get-attribute from object", s, "Failed to evaluate key");
      goto exit;
    }

exit:
  filterx_object_unref(variable);
  return attr;
}

static gboolean
_unset(FilterXExpr *s)
{
  FilterXGetAttr *self = (FilterXGetAttr *) s;

  gboolean result = FALSE;

  FilterXObject *variable = filterx_expr_eval_typed(self->operand);
  if (!variable)
    {
      filterx_eval_push_error_static_info("Failed to unset from object", s, "Failed to evaluate expression");
      return FALSE;
    }

  if (variable->readonly)
    {
      filterx_eval_push_error_static_info("Failed to unset from object", s, "Object is readonly");
      goto exit;
    }

  result = filterx_object_unset_key(variable, self->attr);

exit:
  filterx_object_unref(variable);
  return result;
}

static gboolean
_isset(FilterXExpr *s)
{
  FilterXGetAttr *self = (FilterXGetAttr *) s;
  FilterXObject *variable = filterx_expr_eval_typed(self->operand);
  if (!variable)
    {
      filterx_eval_push_error_static_info("Failed to check element of object", s, "Failed to evaluate expression");
      return FALSE;
    }

  gboolean result = filterx_object_is_key_set(variable, self->attr);

  filterx_object_unref(variable);
  return result;
}

static FilterXExpr *
_optimize(FilterXExpr *s)
{
  FilterXGetAttr *self = (FilterXGetAttr *) s;

  self->operand = filterx_expr_optimize(self->operand);
  return NULL;
}

static gboolean
_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXGetAttr *self = (FilterXGetAttr *) s;

  if (!filterx_expr_init(self->operand, cfg))
    return FALSE;

  return filterx_expr_init_method(s, cfg);
}

static void
_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXGetAttr *self = (FilterXGetAttr *) s;

  filterx_expr_deinit(self->operand, cfg);
  filterx_expr_deinit_method(s, cfg);
}

static void
_free(FilterXExpr *s)
{
  FilterXGetAttr *self = (FilterXGetAttr *) s;
  filterx_object_unref(self->attr);
  filterx_expr_unref(self->operand);
  filterx_expr_free_method(s);
}

static gboolean
_getattr_walk(FilterXExpr *s, FilterXExprWalkOrder order, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXGetAttr *self = (FilterXGetAttr *) s;

  FilterXExpr *exprs[] = { self->operand, NULL };

  for (gsize i = 0; i < G_N_ELEMENTS(exprs); i++)
    {
      if (!filterx_expr_walk(exprs[i], order, f, user_data))
        return FALSE;
    }

  return TRUE;
}

/* NOTE: takes the object reference */
FilterXExpr *
filterx_getattr_new(FilterXExpr *operand, FilterXObject *attr_name)
{
  FilterXGetAttr *self = g_new0(FilterXGetAttr, 1);

  filterx_expr_init_instance(&self->super, FILTERX_EXPR_TYPE_NAME(getattr));
  self->super.eval = _eval_getattr;
  self->super.unset = _unset;
  self->super.is_set = _isset;
  self->super.optimize = _optimize;
  self->super.init = _init;
  self->super.deinit = _deinit;
  self->super.walk_children = _getattr_walk;
  self->super.free_fn = _free;
  self->operand = operand;

  g_assert(filterx_object_is_type(attr_name, &FILTERX_TYPE_NAME(string)));
  self->attr = attr_name;
  /* NOTE: name borrows the string value from the string object */
  self->super.name = filterx_string_get_value_ref(self->attr, NULL);
  return &self->super;
}

FILTERX_EXPR_DEFINE_TYPE(getattr);
