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
#include "filterx/expr-get-subscript-private.h"
#include "filterx/expr-get-subscript-devirt.h"
#include "filterx/filterx-eval.h"

#if SYSLOG_NG_ENABLE_JIT

#include "filterx/jit/jit.h"
#include "filterx/jit/ffi.h"
#include "filterx/object-dict.h"
#include "filterx/object-list.h"

typedef FilterXObject *(*FilterXJITTypedGetSubscript)(FilterXObject *object, FilterXObject *key);

/* The static type that selected a fast path is only a hint. A coercing container (e.g.
 * otel masquerading as dict/list) has a different runtime layout, which the downcast in
 * @typed_get_subscript cannot take, so @expected_type guards it. @expected_type is NULL in
 * the generic helper, which has no fast path and always takes the vtable.
 *
 * _get_subscript_compile() guards @variable against NULL, only @key can still fail here.
 *
 * The helpers unwrap @variable read-only, so the fast path also floats the shared child to
 * keep copy-on-write. The generic fallback floats via the ref vtable. */
static inline __attribute__((always_inline)) FilterXObject *
_do_get_subscript(FilterXObject *variable, FilterXObject *key,
                  FilterXJITTypedGetSubscript typed_get_subscript, FilterXType *expected_type)
{
  if (!key)
    {
      filterx_eval_push_error_static_info("Failed to get-subscript from object",
                                          "Failed to evaluate key");
      filterx_object_unref(variable);
      return NULL;
    }

  FilterXObject *result;
  if (expected_type && filterx_object_is_type_or_ref(variable, expected_type))
    {
      result = typed_get_subscript(variable, key);
      if (result && filterx_object_is_ref(variable))
        result = filterx_ref_replace_shared_xref_with_a_shadow(result, variable);
    }
  else
    result = filterx_object_get_subscript(variable, key);

  if (!result)
    filterx_eval_push_error("Failed to get-subscript from object", key);
  filterx_object_unref(key);
  filterx_object_unref(variable);
  return result;
}

/* No usable static type hint: the vtable does the dispatch, but the operand and the key
 * expressions stay compiled instead of falling back to the interpreter. */
__attribute__((used))
FilterXObject *
fx_jit_do_get_subscript(FilterXObject *variable, FilterXObject *key)
{
  return _do_get_subscript(variable, key, NULL, NULL);
}

__attribute__((used))
FilterXObject *
fx_jit_typed_get_subscript_dict(FilterXObject *variable, FilterXObject *key)
{
  return _do_get_subscript(variable, key, filterx_dict_get_subscript, &FILTERX_TYPE_NAME(dict));
}

__attribute__((used))
FilterXObject *
fx_jit_typed_get_subscript_list(FilterXObject *variable, FilterXObject *key)
{
  return _do_get_subscript(variable, key, filterx_list_get_subscript, &FILTERX_TYPE_NAME(list));
}

FilterXIRValue
_get_subscript_compile(FilterXExpr *s, FilterXJIT *jit)
{
  FilterXGetSubscript *self = (FilterXGetSubscript *) s;
  FilterXJITFFI *ffi = filterx_jit_get_ffi(jit);

  const gchar *fn_name;
  switch (self->operand->static_type)
    {
    case FILTERX_STATIC_TYPE_DICT:
      fn_name = "fx_jit_typed_get_subscript_dict";
      break;
    case FILTERX_STATIC_TYPE_LIST:
      fn_name = "fx_jit_typed_get_subscript_list";
      break;
    default:
      fn_name = "fx_jit_do_get_subscript";
      break;
    }

  /* mirrors _eval_get_subscript(): the key is not evaluated when the operand fails */
  FilterXIRShortCircuit short_circuit;
  fx_jit_emit_short_circuit_begin(jit, &short_circuit, "get_subscript");

  FilterXIRValue variable = filterx_expr_compile_or_eval_typed(self->operand, jit);
  fx_jit_emit_bail_if_null(jit, &short_circuit, variable, NULL);

  FilterXIRValue key = filterx_expr_compile_or_eval_typed(self->key, jit);
  FilterXIRValue args[] = { variable, key };
  FilterXIRType param_tys[] = { ffi->ptr_ty, ffi->ptr_ty };
  FilterXIRValue result = fx_jit_emit_extern_call(jit, fn_name, ffi->ptr_ty, param_tys, args, 2);

  return fx_jit_emit_short_circuit_end(jit, &short_circuit, result);
}

#endif
