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
#include "filterx/expr-variable.h"
#include "filterx/filterx-type-inference.h"
#include "stats/stats-registry.h"
#include "stats/stats-cluster-single.h"

typedef struct _FilterXGetAttr
{
  FilterXExpr super;
  FilterXExpr *operand;
  FilterXObject *attr;
} FilterXGetAttr;

static FilterXObject *
_do_getattr(FilterXObject *variable, FilterXObject *attr, FilterXExpr *expr)
{
  if (!variable)
    {
      filterx_eval_push_error_static_info("Failed to get-attribute from object", expr,
                                          "Failed to evaluate expression");
      return NULL;
    }

  FilterXObject *result = filterx_object_getattr(variable, attr);
  if (!result)
    filterx_eval_push_error_static_info("Failed to get-attribute from object", expr, "Failed to evaluate key");

  filterx_object_unref(variable);
  return result;
}

static FilterXObject *
_eval_getattr(FilterXExpr *s)
{
  FilterXGetAttr *self = (FilterXGetAttr *) s;
  return _do_getattr(filterx_expr_eval_typed(self->operand), self->attr, s);
}

static gboolean
_unset(FilterXExpr *s)
{
  FilterXGetAttr *self = (FilterXGetAttr *) s;

  gboolean result = FALSE;

  FilterXObject *variable = filterx_expr_eval_typed(self->operand);
  if (!variable)
    {
      filterx_eval_push_error_static_info("Failed to unset() from object", s, "Failed to evaluate expression");
      return FALSE;
    }

  result = filterx_object_unset_key(variable, self->attr);
  if (!result)
    {
      filterx_eval_push_error_static_info("Failed to unset() from object", s, "Object does not support unset()");
    }

  filterx_object_unref(variable);
  return result;
}

static FilterXObject *
_move(FilterXExpr *s)
{
  FilterXGetAttr *self = (FilterXGetAttr *) s;
  FilterXObject *result = NULL;

  FilterXObject *variable = filterx_expr_eval_typed(self->operand);
  if (!variable)
    {
      filterx_eval_push_error_static_info("Failed to move() from object", s, "Failed to evaluate expression");
      return NULL;
    }

  result = filterx_object_move_key(variable, self->attr);
  if (!result)
    {
      filterx_eval_push_error_static_info("Failed to move() from object", s, "Object does not support move()");
    }

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

static void
_free(FilterXExpr *s)
{
  FilterXGetAttr *self = (FilterXGetAttr *) s;
  filterx_object_unref(self->attr);
  filterx_expr_unref(self->operand);
  filterx_expr_free_method(s);
}

static gboolean
_getattr_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXGetAttr *self = (FilterXGetAttr *) s;

  FilterXExpr **exprs[] = { &self->operand };

  for (gsize i = 0; i < G_N_ELEMENTS(exprs); i++)
    {
      if (!filterx_expr_visit(s, exprs[i], f, user_data))
        return FALSE;
    }

  return TRUE;
}

static void
_getattr_infer_types(FilterXExpr *s, FilterXTypeEnv *env)
{
  filterx_expr_infer_types_default(s, env);
  FilterXGetAttr *self = (FilterXGetAttr *) s;

  const gchar *key = filterx_string_get_value_ref_and_assert_nul(self->attr, NULL);

  /* Per-key lookup: variable.k0.k1...key.  Covers single-hop accesses (variable.key) as a
   * zero-prefix chain as well as deeper chains where each level was seeded by a setattr in
   * an earlier block. */
  if (self->operand)
    {
      FilterXStaticTypeSpec keyed = filterx_type_env_get_attr_for_chain(env, self->operand, key);
      if (filterx_static_type_kind(keyed) != FILTERX_STATIC_TYPE_UNKNOWN)
        {
          s->static_type = keyed;
          return;
        }
    }

  s->static_type = filterx_static_type_element(self->operand ? self->operand->static_type :
                                               INITIAL_FILTERX_STATIC_TYPE_SPEC);
}

FilterXExpr *
filterx_getattr_get_operand(FilterXExpr *s)
{
  if (!filterx_expr_is_getattr(s))
    return NULL;
  return ((FilterXGetAttr *) s)->operand;
}

#if SYSLOG_NG_ENABLE_JIT

#include "filterx/jit/jit.h"
#include "filterx/jit/ffi.h"
#include "filterx/object-dict.h"

__attribute__((used))
FilterXObject *
fx_jit_do_getattr(FilterXObject *variable, FilterXObject *attr, FilterXExpr *expr)
{
  return _do_getattr(variable, attr, expr);
}

/* Dict-specialized fast path. dict.attr is semantically dict[attr] with a known-string key,
 * so we call filterx_dict_get_subscript_unchecked directly, bypassing the mapping's
 * getattr → get_subscript hop. @attr is borrowed (owned by the FilterXGetAttr struct) and
 * must not be unrefed.
 *
 * The static type DICT is only a hint: coercing containers (e.g. otel_logrecord.body, which
 * stores an assigned dict as an otel_kvlist) yield a non-dict object at runtime. Since the
 * unchecked path downcasts to FilterXDictObject, verify the runtime layout first and fall
 * back to the generic vtable getattr otherwise.
 *
 * The unchecked lookup unwraps @variable read-only, so when @variable is a ref we must
 * still float the shared child against it (exactly as the ref's getattr vtable does) to
 * preserve copy-on-write; otherwise a subsequent setattr on the returned child would mutate
 * a dict still shared with the source variable. (_do_getattr's generic path floats internally
 * via the ref vtable, so the fallback needs no explicit float.) */
__attribute__((used))
FilterXObject *
fx_jit_do_getattr_dict(FilterXObject *variable, FilterXObject *attr, FilterXExpr *expr)
{
  if (!variable)
    {
      filterx_eval_push_error_static_info("Failed to get-attribute from object", expr,
                                          "Failed to evaluate expression");
      return NULL;
    }

  if (!filterx_object_is_type_or_ref(variable, &FILTERX_TYPE_NAME(dict)))
    return _do_getattr(variable, attr, expr);

  FilterXObject *result = filterx_dict_get_subscript_unchecked(variable, attr);
  if (!result)
    filterx_eval_push_error_static_info("Failed to get-attribute from object", expr,
                                        "Failed to evaluate key");
  else if (filterx_object_is_ref(variable))
    result = filterx_ref_float_shared_child(result, variable);

  filterx_object_unref(variable);
  return result;
}

static FilterXIRValue
_getattr_compile(FilterXExpr *s, FilterXJIT *jit)
{
  FilterXGetAttr *self = (FilterXGetAttr *) s;
  FilterXJITFFI *ffi = filterx_jit_get_ffi(jit);

  const gchar *fn_name = filterx_static_type_kind(self->operand->static_type) == FILTERX_STATIC_TYPE_DICT
                         ? "fx_jit_do_getattr_dict"
                         : "fx_jit_do_getattr";

  FilterXIRValue variable = filterx_expr_compile_or_eval_typed(self->operand, jit);
  FilterXIRValue args[] =
  {
    variable,
    fx_jit_emit_const_ptr(jit, self->attr),
    fx_jit_emit_const_ptr(jit, self),
  };
  FilterXIRType param_tys[] = { ffi->ptr_ty, ffi->ptr_ty, ffi->ptr_ty };
  return fx_jit_emit_extern_call(jit, fn_name, ffi->ptr_ty, param_tys, args, 3);
}

#endif

/* NOTE: takes the object reference */
FilterXExpr *
filterx_getattr_new(FilterXExpr *operand, FilterXObject *attr_name)
{
  FilterXGetAttr *self = g_new0(FilterXGetAttr, 1);

  filterx_expr_init_instance(&self->super, FILTERX_EXPR_TYPE_NAME(getattr), FXE_READ);
  self->super.eval = _eval_getattr;
  self->super.unset = _unset;
  self->super.move = _move;
  self->super.is_set = _isset;
  self->super.walk_children = _getattr_walk;
  self->super.free_fn = _free;
  self->super.infer_types = _getattr_infer_types;
#if SYSLOG_NG_ENABLE_JIT
  self->super.compile = _getattr_compile;
#endif
  self->operand = operand;

  g_assert(filterx_object_is_type(attr_name, &FILTERX_TYPE_NAME(string)));
  self->attr = attr_name;
  /* NOTE: name borrows the string value from the string object */
  self->super.name = filterx_string_get_value_ref_and_assert_nul(self->attr, NULL);
  return &self->super;
}

FILTERX_EXPR_DEFINE_TYPE(getattr);
