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
#include "filterx/expr-set-subscript-private.h"
#include "filterx/expr-set-subscript-devirt.h"
#include "filterx/filterx-eval.h"

#if SYSLOG_NG_ENABLE_JIT

#include "filterx/jit/jit.h"
#include "filterx/jit/ffi.h"
#include "filterx/object-dict.h"
#include "filterx/object-list.h"

typedef gboolean (*FilterXJITTypedSetSubscript)(FilterXObject *object, FilterXObject *key, FilterXObject **value);

/* The static type that selected a fast path is only a hint. A coercing container (e.g.
 * otel masquerading as dict/list) has a different runtime layout, which the downcast in
 * @typed_set_subscript cannot take, so @expected_type guards it. The generic vtable
 * set_subscript has the same signature and serves as the fallback. @expected_type is NULL
 * in the generic helper, which has no fast path and always takes the vtable.
 *
 * _compile_dispatch() guards @cloned and @key against NULL, only @object can still fail
 * here. A NULL @key is the keyless append form (`list[] = value`), which both the vtable and
 * the list fast path take as "append to the tail".
 *
 * @cloned is the already forked right hand side, see the NOTE in _compile_dispatch().
 * We own it, together with @object and @key, and we return it on success. */
static inline __attribute__((always_inline)) FilterXObject *
_do_set_subscript(FilterXObject *object, FilterXObject *key, FilterXObject *cloned,
                  FilterXJITTypedSetSubscript typed_set_subscript, FilterXType *expected_type)
{
  if (!object)
    {
      filterx_eval_push_error_static_info("Failed to set element of object",
                                          "Failed to evaluate expression");
      goto error;
    }
  if (!expected_type || !filterx_object_is_type_or_ref(object, expected_type))
    typed_set_subscript = filterx_object_set_subscript;
  if (!typed_set_subscript(object, key, &cloned))
    {
      filterx_eval_push_error("Object set-subscript failed", key);
      filterx_eval_push_error_static_info("Failed to set element of object",
                                          "set-subscript() method failed");
      goto error;
    }

  filterx_object_unref(object);
  filterx_object_unref(key);
  return cloned;

error:
  filterx_object_unref(object);
  filterx_object_unref(key);
  filterx_object_unref(cloned);
  return NULL;
}

__attribute__((used))
FilterXObject *
fx_jit_typed_set_subscript_dict(FilterXObject *object, FilterXObject *key, FilterXObject *cloned)
{
  return _do_set_subscript(object, key, cloned, filterx_dict_set_subscript, &FILTERX_TYPE_NAME(dict));
}

__attribute__((used))
FilterXObject *
fx_jit_typed_set_subscript_list(FilterXObject *object, FilterXObject *key, FilterXObject *cloned)
{
  return _do_set_subscript(object, key, cloned, filterx_list_set_subscript_by_key, &FILTERX_TYPE_NAME(list));
}

/* No usable static type hint: the vtable does the dispatch, but the object, the key and the
 * right hand side expressions stay compiled instead of falling back to the interpreter. */
__attribute__((used))
FilterXObject *
fx_jit_do_set_subscript(FilterXObject *object, FilterXObject *key, FilterXObject *cloned)
{
  return _do_set_subscript(object, key, cloned, NULL, NULL);
}

static FilterXIRValue
_compile_dispatch(FilterXExpr *s, FilterXJIT *jit, gboolean nullv)
{
  FilterXSetSubscript *self = (FilterXSetSubscript *) s;
  FilterXJITFFI *ffi = filterx_jit_get_ffi(jit);

  const gchar *fn_name;
  switch (self->object->static_type)
    {
    case FILTERX_STATIC_TYPE_DICT:
      fn_name = "fx_jit_typed_set_subscript_dict";
      break;
    case FILTERX_STATIC_TYPE_LIST:
      fn_name = "fx_jit_typed_set_subscript_list";
      break;
    default:
      fn_name = "fx_jit_do_set_subscript";
      break;
    }

  FilterXIRShortCircuit short_circuit;
  fx_jit_emit_short_circuit_begin(jit, &short_circuit, "set_subscript");

  /* NOTE: the fork has to happen before we evaluate the lhs, so that the lhs will notice it
   * is shared and can clone accordingly.  This is needed to make sure something like
   * `d["sub"] = d` works.  _set_subscript() in the interpreter forks new_value for the very
   * same reason.
   */
  FilterXIRValue new_value = filterx_expr_compile_or_eval(self->new_value, jit);
  if (nullv)
    {
      /* mirrors _nullv_set_subscript_eval(): a right hand side that failed or that is a null
       * object leaves the key and the object unevaluated */
      fx_jit_emit_bail_if_rhs_suppressed(jit, &short_circuit, new_value);
      fx_jit_emit_bail_if_rhs_null_object(jit, &short_circuit, new_value, NULL);
    }
  else
    fx_jit_emit_bail_if_null(jit, &short_circuit, new_value, NULL);

  /* the keyless append form has no key expression, it passes a NULL key down */
  FilterXIRValue key = fx_jit_emit_const_ptr(jit, NULL);
  if (self->key)
    {
      key = filterx_expr_compile_or_eval(self->key, jit);
      fx_jit_emit_bail_if_null(jit, &short_circuit, key, new_value);
    }

  FilterXIRValue cloned = fx_jit_emit_object_cow_fork2(jit, new_value);
  FilterXIRValue object = filterx_expr_compile_or_eval_typed(self->object, jit);

  FilterXIRValue args[] = { object, key, cloned };
  FilterXIRType param_tys[] = { ffi->ptr_ty, ffi->ptr_ty, ffi->ptr_ty };
  FilterXIRValue result = fx_jit_emit_extern_call(jit, fn_name, ffi->ptr_ty, param_tys, args, 3);

  return fx_jit_emit_short_circuit_end(jit, &short_circuit, result);
}

FilterXIRValue
_set_subscript_compile(FilterXExpr *s, FilterXJIT *jit)
{
  return _compile_dispatch(s, jit, FALSE);
}

FilterXIRValue
_nullv_set_subscript_compile(FilterXExpr *s, FilterXJIT *jit)
{
  return _compile_dispatch(s, jit, TRUE);
}

#endif
