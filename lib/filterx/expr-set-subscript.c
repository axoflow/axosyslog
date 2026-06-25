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
#include "filterx/object-dict.h"
#include "filterx/object-list.h"
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

  if (!filterx_object_set_subscript(object, key, &cloned))
    {
      filterx_eval_push_error("Object set-subscript failed", &self->super, key);
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

static void
_set_subscript_infer_types(FilterXExpr *s, FilterXTypeEnv *env)
{
  filterx_expr_infer_types_default(s, env);
  FilterXSetSubscript *self = (FilterXSetSubscript *) s;

  /* Refine the written container's element type with the assigned value (lift if the level
   * was a freshly-built empty container, meet otherwise). This keeps element-type info alive
   * across incremental builds (l = []; l[0] = {}; l[0].x = {}; ...) so deeper accesses
   * devirtualize. */
  filterx_type_env_update_on_write(env, self->object,
                                   self->new_value ? self->new_value->static_type : INITIAL_FILTERX_STATIC_TYPE_SPEC);
}

#if SYSLOG_NG_ENABLE_JIT

#include "filterx/jit/jit.h"
#include "filterx/jit/ffi.h"

typedef gboolean (*FXSetSubscriptHelper)(FilterXObject *object, FilterXObject *key, FilterXObject **value);

/* @helper downcasts @object to a concrete FilterX{Dict,List}Object; the static type that
 * selected this fast path is only a hint, so coercing containers (e.g. otel masquerading as
 * dict/list) may present a different runtime layout. @expected_type guards the downcast and
 * on a mismatch we fall back to the generic vtable set_subscript (same signature). */
static inline __attribute__((always_inline)) FilterXObject *
_do_set_subscript(FilterXObject *object, FilterXObject *key, FilterXObject *new_value, FilterXExpr *expr,
                  FXSetSubscriptHelper helper, FilterXType *expected_type)
{
  FilterXObject *cloned = NULL;
  if (!new_value)
    {
      filterx_eval_push_error_static_info("Failed to set element of object", expr,
                                          "Failed to evaluate right hand side");
      goto cleanup;
    }
  if (!key)
    {
      filterx_eval_push_error_static_info("Failed to set element of object", expr,
                                          "Failed to evaluate key");
      goto cleanup;
    }
  if (!object)
    {
      filterx_eval_push_error_static_info("Failed to set element of object", expr,
                                          "Failed to evaluate expression");
      goto cleanup;
    }
  if (!filterx_object_is_type_or_ref(object, expected_type))
    helper = filterx_object_set_subscript;
  cloned = filterx_object_cow_fork2(filterx_object_ref(new_value), NULL);
  if (!helper(object, key, &cloned))
    {
      filterx_eval_push_error("Object set-subscript failed", expr, key);
      filterx_eval_push_error_static_info("Failed to set element of object", expr,
                                          "set-subscript() method failed");
      filterx_object_unref(cloned);
      cloned = NULL;
    }
cleanup:
  filterx_object_unref(object);
  filterx_object_unref(key);
  filterx_object_unref(new_value);
  return cloned;
}

__attribute__((used))
FilterXObject *
fx_jit_do_set_subscript_dict(FilterXObject *object, FilterXObject *key, FilterXObject *new_value, FilterXExpr *expr)
{
  return _do_set_subscript(object, key, new_value, expr, filterx_dict_set_subscript, &FILTERX_TYPE_NAME(dict));
}

__attribute__((used))
FilterXObject *
fx_jit_do_set_subscript_list(FilterXObject *object, FilterXObject *key, FilterXObject *new_value, FilterXExpr *expr)
{
  return _do_set_subscript(object, key, new_value, expr, filterx_list_set_subscript_via_key, &FILTERX_TYPE_NAME(list));
}

typedef FilterXObject *(*FXSetSubscriptWrapper)(FilterXObject *object, FilterXObject *key,
                                                FilterXObject *new_value, FilterXExpr *expr);

static inline __attribute__((always_inline)) FilterXObject *
_do_nullv_set_subscript(FilterXObject *object, FilterXObject *key, FilterXObject *new_value, FilterXExpr *expr,
                        FXSetSubscriptWrapper wrapper)
{
  if (!new_value)
    {
      filterx_eval_dump_errors("FilterX: null coalesce assignment suppressing error");
      filterx_object_unref(object);
      filterx_object_unref(key);
      return filterx_null_new();
    }
  if (filterx_object_extract_null(new_value))
    {
      filterx_object_unref(object);
      filterx_object_unref(key);
      return new_value;
    }
  return wrapper(object, key, new_value, expr);
}

__attribute__((used))
FilterXObject *
fx_jit_do_nullv_set_subscript_dict(FilterXObject *object, FilterXObject *key,
                                   FilterXObject *new_value, FilterXExpr *expr)
{
  return _do_nullv_set_subscript(object, key, new_value, expr, fx_jit_do_set_subscript_dict);
}

__attribute__((used))
FilterXObject *
fx_jit_do_nullv_set_subscript_list(FilterXObject *object, FilterXObject *key,
                                   FilterXObject *new_value, FilterXExpr *expr)
{
  return _do_nullv_set_subscript(object, key, new_value, expr, fx_jit_do_set_subscript_list);
}

__attribute__((used)) __attribute__((noinline))
gint32
fx_jit_do_set_subscript_dict_stmt(FilterXObject *object, FilterXObject *key, FilterXObject *new_value,
                                  FilterXExpr *expr, FilterXEvalContext *context, FilterXObject **last_result)
{
  return fx_jit_process_expr_result(fx_jit_do_set_subscript_dict(object, key, new_value, expr), expr, context,
                                    last_result);
}

__attribute__((used)) __attribute__((noinline))
gint32
fx_jit_do_set_subscript_list_stmt(FilterXObject *object, FilterXObject *key, FilterXObject *new_value,
                                  FilterXExpr *expr, FilterXEvalContext *context, FilterXObject **last_result)
{
  return fx_jit_process_expr_result(fx_jit_do_set_subscript_list(object, key, new_value, expr), expr, context,
                                    last_result);
}

__attribute__((used)) __attribute__((noinline))
gint32
fx_jit_do_nullv_set_subscript_dict_stmt(FilterXObject *object, FilterXObject *key, FilterXObject *new_value,
                                        FilterXExpr *expr, FilterXEvalContext *context, FilterXObject **last_result)
{
  return fx_jit_process_expr_result(fx_jit_do_nullv_set_subscript_dict(object, key, new_value, expr), expr, context,
                                    last_result);
}

__attribute__((used)) __attribute__((noinline))
gint32
fx_jit_do_nullv_set_subscript_list_stmt(FilterXObject *object, FilterXObject *key, FilterXObject *new_value,
                                        FilterXExpr *expr, FilterXEvalContext *context, FilterXObject **last_result)
{
  return fx_jit_process_expr_result(fx_jit_do_nullv_set_subscript_list(object, key, new_value, expr), expr, context,
                                    last_result);
}

static FilterXIRValue
_compile_dispatch(FilterXExpr *s, FilterXJIT *jit, const gchar *fn_dict, const gchar *fn_list)
{
  FilterXSetSubscript *self = (FilterXSetSubscript *) s;
  FilterXJITFFI *ffi = filterx_jit_get_ffi(jit);

  /* v1: only specialize when we have a key expression. set_subscript with self->key == NULL
   * (e.g. list-append form) takes the interpreter path. */
  const gchar *fn_name;
  switch (filterx_static_type_kind(self->object->static_type))
    {
    case FILTERX_STATIC_TYPE_DICT:
      fn_name = fn_dict;
      break;
    case FILTERX_STATIC_TYPE_LIST:
      fn_name = fn_list;
      break;
    default:
      return fx_jit_emit_expr_eval(jit, s);
    }

  if (!self->key)
    return fx_jit_emit_expr_eval(jit, s);

  /* Eval order matches the interpreter: new_value first, then key, then object. */
  FilterXIRValue new_value = filterx_expr_compile_or_eval(self->new_value, jit);
  FilterXIRValue key = filterx_expr_compile_or_eval(self->key, jit);
  FilterXIRValue object = filterx_expr_compile_or_eval_typed(self->object, jit);

  FilterXIRValue args[] = { object, key, new_value, fx_jit_emit_const_ptr(jit, s) };
  FilterXIRType param_tys[] = { ffi->ptr_ty, ffi->ptr_ty, ffi->ptr_ty, ffi->ptr_ty };
  return fx_jit_emit_extern_call(jit, fn_name, ffi->ptr_ty, param_tys, args, 4);
}

static FilterXIRValue
_set_subscript_compile(FilterXExpr *s, FilterXJIT *jit)
{
  return _compile_dispatch(s, jit, "fx_jit_do_set_subscript_dict",
                           "fx_jit_do_set_subscript_list");
}

static FilterXIRValue
_nullv_set_subscript_compile(FilterXExpr *s, FilterXJIT *jit)
{
  return _compile_dispatch(s, jit, "fx_jit_do_nullv_set_subscript_dict",
                           "fx_jit_do_nullv_set_subscript_list");
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
  self->super.infer_types = _set_subscript_infer_types;
#if SYSLOG_NG_ENABLE_JIT
  self->super.compile = _set_subscript_compile;
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
#if SYSLOG_NG_ENABLE_JIT
  self->compile = _nullv_set_subscript_compile;
#endif
  return self;
}
