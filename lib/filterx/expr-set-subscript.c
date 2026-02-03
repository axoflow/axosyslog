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
#include "filterx/expr-set-subscript.h"
#include "filterx/object-primitive.h"
#include "filterx/filterx-eval.h"
#include "filterx/object-null.h"
#include "filterx/object-message-value.h"
#include "filterx/object-extractor.h"
#include "scratch-buffers.h"
#include "stats/stats-registry.h"
#include "stats/stats-cluster-single.h"

typedef struct _FilterXSetSubscript
{
  FilterXExpr super;
  FilterXExpr *object;
  FilterXExpr *key;
  FilterXExpr *new_value;
} FilterXSetSubscript;

static inline FilterXObject *
_set_subscript(FilterXSetSubscript *self, FilterXObject *key, FilterXObject *new_value)
{
  FilterXObject *cloned = filterx_object_cow_fork2(filterx_object_ref(new_value), NULL);

  FilterXObject *object = filterx_expr_eval_typed(self->object);
  if (!object)
    {
      filterx_eval_push_error_static_info("Failed to set element of object", &self->super, "Failed to evaluate expression");
      goto error;
    }

  if (object->readonly)
    {
      filterx_eval_push_error("Object set-subscript failed, object is readonly", &self->super, key);
      goto error;
    }

  if (!filterx_object_set_subscript(object, key, &cloned))
    {
      filterx_eval_push_error("Object set-subscript failed", &self->super, key);
      goto error;
    }

  return cloned;
error:
  filterx_object_unref(object);
  filterx_object_unref(cloned);
  return NULL;
}

static inline FilterXObject *
_suppress_error(void)
{
  filterx_eval_dump_errors("FilterX: null coalesce assignment suppressing error");

  return filterx_null_new();
}

static FilterXObject *
_nullv_set_subscript_eval(FilterXExpr *s)
{
  FilterXSetSubscript *self = (FilterXSetSubscript *) s;
  FilterXObject *result = NULL;
  FilterXObject *key = NULL;

  FilterXObject *new_value = filterx_expr_eval(self->new_value);
  if (!new_value || filterx_object_extract_null(new_value))
    {
      if (!new_value)
        return _suppress_error();

      return new_value;
    }

  if (self->key)
    {
      key = filterx_expr_eval(self->key);
      if (!key)
        {
          filterx_eval_push_error_static_info("Failed to set element of object", s, "Failed to evaluate key");
          goto exit;
        }
    }

  result = _set_subscript(self, key, new_value);
  if (!result)
    {
      filterx_eval_push_error_static_info("Failed to set element of object", s, "set-subscript() method failed");
      goto exit;
    }

exit:
  filterx_object_unref(new_value);
  filterx_object_unref(key);
  return result;
}

static FilterXObject *
_set_subscript_eval(FilterXExpr *s)
{
  FilterXSetSubscript *self = (FilterXSetSubscript *) s;
  FilterXObject *result = NULL;
  FilterXObject *key = NULL;

  FilterXObject *new_value = filterx_expr_eval(self->new_value);
  if (!new_value)
    {
      filterx_eval_push_error_static_info("Failed to set element of object", s, "Failed to evaluate right hand side");
      return NULL;
    }

  if (self->key)
    {
      key = filterx_expr_eval(self->key);
      if (!key)
        {
          filterx_eval_push_error_static_info("Failed to set element of object", s, "Failed to evaluate key");
          goto exit;
        }
    }

  result = _set_subscript(self, key, new_value);
  if (!result)
    {
      filterx_eval_push_error_static_info("Failed to set element of object", s, "set-subscript() method failed");
      goto exit;
    }

exit:
  filterx_object_unref(new_value);
  filterx_object_unref(key);
  return result;
}

static void
_free(FilterXExpr *s)
{
  FilterXSetSubscript *self = (FilterXSetSubscript *) s;

  filterx_expr_unref(self->key);
  filterx_expr_unref(self->object);
  filterx_expr_unref(self->new_value);
  filterx_expr_free_method(s);
}

static gboolean
_set_subscript_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXSetSubscript *self = (FilterXSetSubscript *) s;

  FilterXExpr **exprs[] = { &self->object, &self->key, &self->new_value };

  for (gsize i = 0; i < G_N_ELEMENTS(exprs); i++)
    {
      if (!filterx_expr_visit(s, exprs[i], f, user_data))
        return FALSE;
    }

  return TRUE;
}

FilterXExpr *
filterx_set_subscript_new(FilterXExpr *object, FilterXExpr *key, FilterXExpr *new_value)
{
  FilterXSetSubscript *self = g_new0(FilterXSetSubscript, 1);

  filterx_expr_init_instance(&self->super, "set_subscript", FXE_WRITE);
  self->super.eval = _set_subscript_eval;
  self->super.walk_children = _set_subscript_walk;
  self->super.free_fn = _free;
  self->object = object;
  self->key = key;
  self->new_value = new_value;
  self->super.ignore_falsy_result = TRUE;
  return &self->super;
}

FilterXExpr *
filterx_nullv_set_subscript_new(FilterXExpr *object, FilterXExpr *key, FilterXExpr *new_value)
{
  FilterXExpr *self = filterx_set_subscript_new(object, key, new_value);

  self->type = "nullv_set_subscript";
  self->eval = _nullv_set_subscript_eval;
  return self;
}
