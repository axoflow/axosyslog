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
#include "filterx/expr-variable.h"
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
      goto error;
    }

  if (!filterx_object_set_subscript(object, key, &cloned))
    {
      filterx_eval_push_error("Object set-subscript failed", key);
      goto error;
    }

  filterx_object_unref(object);
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
          goto exit;
        }
    }

  result = _set_subscript(self, key, new_value);
  if (!result)
    {
      filterx_eval_push_error_static_info("Failed to set element of object", "set-subscript() method failed");
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
      return NULL;
    }

  if (self->key)
    {
      key = filterx_expr_eval(self->key);
      if (!key)
        {
          goto exit;
        }
    }

  result = _set_subscript(self, key, new_value);
  if (!result)
    {
      filterx_eval_push_error_static_info("Failed to set element of object", "set-subscript() method failed");
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

/* The location this statement writes: one hop down its object, at the key it sets.  A literal
 * string key names the very step a getattr would, so `$a["b"] = 1` and `$a.b = 1` leave identical
 * state.  Every other key -- a computed one, a list index, and the `$a[] = 1` append form whose
 * key expression really is NULL -- names no step, so the path stops truncated at the object and
 * the write retires the object's whole interior.
 *
 * Naming itself is safe for the same reason a setattr may: the grammar reaches an assignment only
 * from stmt_expr, so this node is always a statement and never the operand of a read. */
static gboolean
_set_subscript_get_path(FilterXExpr *s, FilterXTypePath *path_out)
{
  FilterXSetSubscript *self = (FilterXSetSubscript *) s;

  if (!filterx_expr_get_path(self->object, path_out))
    return FALSE;

  filterx_type_path_append_step(path_out, filterx_type_path_step_from_key_expr(self->key));
  return TRUE;
}

#if SYSLOG_NG_ENABLE_JIT
static void
_set_subscript_record_write(FilterXSetSubscript *self, FilterXTypeEnv *env)
{
  FilterXTypePath path;

  if (filterx_expr_get_path(&self->super, &path))
    filterx_type_env_set_shape_at_path(env, &path, self->new_value);
}

static void
_set_subscript_infer_types(FilterXExpr *s, FilterXTypeEnv *env)
{
  filterx_expr_infer_types_default(s, env);
  _set_subscript_record_write((FilterXSetSubscript *) s, env);
}

#endif

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
  self->super.get_path = _set_subscript_get_path;
#if SYSLOG_NG_ENABLE_JIT
  self->super.infer_types = _set_subscript_infer_types;
#endif
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
