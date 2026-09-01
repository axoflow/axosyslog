/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 Attila Szakacs <attila.szakacs@axoflow.com>
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

#include "filterx/expr-unset.h"
#include "filterx/object-primitive.h"

typedef struct FilterXExprUnset_
{
  FilterXFunction super;
  GPtrArray *exprs;
} FilterXExprUnset;

static FilterXObject *
_eval_unset(FilterXExpr *s)
{
  FilterXExprUnset *self = (FilterXExprUnset *) s;

  for (guint i = 0; i < self->exprs->len; i++)
    {
      FilterXExpr *expr = (FilterXExpr *) g_ptr_array_index(self->exprs, i);
      if (!filterx_expr_unset(expr))
        return NULL;
    }

  return filterx_boolean_new(TRUE);
}

static void
_free(FilterXExpr *s)
{
  FilterXExprUnset *self = (FilterXExprUnset *) s;

  g_ptr_array_unref(self->exprs);
  filterx_function_free_method(&self->super);
}

#if SYSLOG_NG_ENABLE_JIT
static void
_unset_infer_types(FilterXExpr *s, FilterXTypeEnv *env)
{
  FilterXExprUnset *self = (FilterXExprUnset *) s;

  filterx_expr_infer_types_default(s, env);

  for (guint i = 0; i < self->exprs->len; i++)
    filterx_type_env_update_on_remove(env, (FilterXExpr *) g_ptr_array_index(self->exprs, i));

  s->static_type = FILTERX_STATIC_TYPE_BOOLEAN;
}
#endif

gboolean
_unset_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXExprUnset *self = (FilterXExprUnset *) s;

  for (guint i = 0; i < self->exprs->len; i++)
    {
      FilterXExpr **expr = (FilterXExpr **) &g_ptr_array_index(self->exprs, i);
      if (!filterx_expr_visit(s, expr, f, user_data))
        return FALSE;
    }

  return TRUE;
}

FilterXExpr *
filterx_function_unset_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXExprUnset *self = g_new0(FilterXExprUnset, 1);
  filterx_function_init_instance(&self->super, "unset", FXE_WRITE);

  self->super.super.eval = _eval_unset;
  self->super.super.walk_children = _unset_walk;
  self->super.super.free_fn = _free;
#if SYSLOG_NG_ENABLE_JIT
  self->super.super.infer_types = _unset_infer_types;
#endif

  self->exprs = g_ptr_array_new_full(filterx_function_args_len(args), (GDestroyNotify) filterx_expr_unref);
  for (guint64 i = 0; i < filterx_function_args_len(args); i++)
    {
      FilterXExpr *expr = filterx_function_args_get_expr(args, i);
      if (!filterx_expr_unset_available(expr))
        {
          g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                      "expected argument %d to be unsettable", (gint) i);
          goto error;
        }
      g_ptr_array_add(self->exprs, expr);
    }

  if (!filterx_function_args_check(args, error))
    goto error;

  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}
