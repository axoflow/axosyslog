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
#include "filterx/object-dict.h"
#include "filterx/object-list.h"
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
      filterx_eval_push_error_static_info("Failed to unset() from object", s, "Failed to evaluate expression");
      return FALSE;
    }

  FilterXObject *key = filterx_expr_eval_typed(self->key);
  if (!key)
    {
      filterx_eval_push_error_static_info("Failed to unset() from object", s, "Failed to evaluate key");
      goto exit;
    }

  result = filterx_object_unset_key(variable, key);
  if (!result)
    {
      filterx_eval_push_error_static_info("Failed to unset() from object", s, "Object does not support unset()");
      goto exit;
    }

exit:
  filterx_object_unref(key);
  filterx_object_unref(variable);
  return result;
}

static FilterXObject *
_move(FilterXExpr *s)
{
  FilterXGetSubscript *self = (FilterXGetSubscript *) s;

  FilterXObject *result = NULL;

  FilterXObject *variable = filterx_expr_eval_typed(self->operand);
  if (!variable)
    {
      filterx_eval_push_error_static_info("Failed to move() from object", s, "Failed to evaluate expression");
      return FALSE;
    }

  FilterXObject *key = filterx_expr_eval_typed(self->key);
  if (!key)
    {
      filterx_eval_push_error_static_info("Failed to move() from object", s, "Failed to evaluate key");
      goto exit;
    }

  result = filterx_object_move_key(variable, key);
  if (!result)
    {
      filterx_eval_push_error_static_info("Failed to move() from object", s, "Object does not support move()");
      goto exit;
    }

exit:
  filterx_object_unref(key);
  filterx_object_unref(variable);
  return result;
}

static void
_free(FilterXExpr *s)
{
  FilterXGetSubscript *self = (FilterXGetSubscript *) s;
  filterx_expr_unref(self->key);
  filterx_expr_unref(self->operand);
  filterx_expr_free_method(s);
}

static void
_get_subscript_infer_types(FilterXExpr *s, FilterXTypeEnv *env)
{
  filterx_expr_infer_types_default(s, env);
  FilterXGetSubscript *self = (FilterXGetSubscript *) s;
  /* Result is one nesting level shallower than the operand. */
  s->static_type = filterx_static_type_element(self->operand ? self->operand->static_type :
                                               INITIAL_FILTERX_STATIC_TYPE_SPEC);
}

FilterXExpr *
filterx_get_subscript_get_operand(FilterXExpr *s)
{
  if (!filterx_expr_is_get_subscript(s))
    return NULL;
  return ((FilterXGetSubscript *) s)->operand;
}

#if SYSLOG_NG_ENABLE_JIT

#include "filterx/jit/jit.h"
#include "filterx/jit/ffi.h"

typedef FilterXObject *(*FXGetSubscriptHelper)(FilterXObject *object, FilterXObject *key);

static inline __attribute__((always_inline)) FilterXObject *
_do_get_subscript(FilterXObject *variable, FilterXObject *key, FilterXExpr *expr,
                  FXGetSubscriptHelper helper)
{
  if (!variable)
    {
      filterx_eval_push_error_static_info("Failed to get-subscript from object", expr,
                                          "Failed to evaluate expression");
      return NULL;
    }
  if (!key)
    {
      filterx_eval_push_error_static_info("Failed to get-subscript from object", expr,
                                          "Failed to evaluate key");
      filterx_object_unref(variable);
      return NULL;
    }
  FilterXObject *result = helper(variable, key);
  if (!result)
    filterx_eval_push_error("Failed to get-subscript from object", expr, key);
  filterx_object_unref(key);
  filterx_object_unref(variable);
  return result;
}

__attribute__((used))
FilterXObject *
fx_jit_do_get_subscript_dict(FilterXObject *variable, FilterXObject *key, FilterXExpr *expr)
{
  return _do_get_subscript(variable, key, expr, filterx_dict_get_subscript);
}

__attribute__((used))
FilterXObject *
fx_jit_do_get_subscript_list(FilterXObject *variable, FilterXObject *key, FilterXExpr *expr)
{
  return _do_get_subscript(variable, key, expr, filterx_list_get_subscript);
}

__attribute__((used))
FilterXObject *
fx_jit_do_get_subscript_dict_string_key(FilterXObject *variable, FilterXObject *key, FilterXExpr *expr)
{
  return _do_get_subscript(variable, key, expr, filterx_dict_get_subscript_unchecked);
}

static FilterXIRValue
_get_subscript_compile(FilterXExpr *s, FilterXJIT *jit)
{
  FilterXGetSubscript *self = (FilterXGetSubscript *) s;
  FilterXJITFFI *ffi = filterx_jit_get_ffi(jit);

  const gchar *fn_name;
  switch (filterx_static_type_kind(self->operand->static_type))
    {
    case FILTERX_STATIC_TYPE_DICT:
      /* Dict keys must be hashable strings at runtime. When inference proves the key is a
       * string, skip the runtime hashable check by dispatching to the unchecked variant. */
      fn_name = filterx_static_type_kind(self->key->static_type) == FILTERX_STATIC_TYPE_STRING
                ? "fx_jit_do_get_subscript_dict_string_key"
                : "fx_jit_do_get_subscript_dict";
      break;
    case FILTERX_STATIC_TYPE_LIST:
      fn_name = "fx_jit_do_get_subscript_list";
      break;
    default:
      return fx_jit_emit_expr_eval(jit, s);
    }

  FilterXIRValue variable = filterx_expr_compile_or_eval_typed(self->operand, jit);
  FilterXIRValue key = filterx_expr_compile_or_eval_typed(self->key, jit);
  FilterXIRValue args[] = { variable, key, fx_jit_emit_const_ptr(jit, s) };
  FilterXIRType param_tys[] = { ffi->ptr_ty, ffi->ptr_ty, ffi->ptr_ty };
  return fx_jit_emit_extern_call(jit, fn_name, ffi->ptr_ty, param_tys, args, 3);
}

#endif

static gboolean
_get_subscript_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXGetSubscript *self = (FilterXGetSubscript *) s;

  FilterXExpr **exprs[] = { &self->operand, &self->key };

  for (gsize i = 0; i < G_N_ELEMENTS(exprs); i++)
    {
      if (!filterx_expr_visit(s, exprs[i], f, user_data))
        return FALSE;
    }

  return TRUE;
}

/* NOTE: takes the object reference */
FilterXExpr *
filterx_get_subscript_new(FilterXExpr *operand, FilterXExpr *key)
{
  FilterXGetSubscript *self = g_new0(FilterXGetSubscript, 1);

  filterx_expr_init_instance(&self->super, FILTERX_EXPR_TYPE_NAME(get_subscript), FXE_READ);
  self->super.eval = _eval_get_subscript;
  self->super.is_set = _isset;
  self->super.unset = _unset;
  self->super.walk_children = _get_subscript_walk;
  self->super.move = _move;
  self->super.free_fn = _free;
  self->super.infer_types = _get_subscript_infer_types;
#if SYSLOG_NG_ENABLE_JIT
  self->super.compile = _get_subscript_compile;
#endif
  self->operand = operand;
  self->key = key;
  return &self->super;
}

FILTERX_EXPR_DEFINE_TYPE(get_subscript);
