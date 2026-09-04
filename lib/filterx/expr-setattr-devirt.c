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
#include "filterx/expr-setattr-private.h"
#include "filterx/expr-setattr-devirt.h"
#include "filterx/filterx-eval.h"
#include "filterx/object-extractor.h"

#if SYSLOG_NG_ENABLE_JIT

#include "filterx/jit/jit.h"
#include "filterx/jit/ffi.h"
#include "filterx/object-dict.h"

__attribute__((used))
FilterXObject *
fx_jit_do_setattr(FilterXExpr *s, FilterXObject *lhs, FilterXObject *cloned)
{
  return _do_setattr((FilterXSetAttr *) s, lhs, cloned);
}

__attribute__((used))
FilterXObject *
fx_jit_do_nullv_setattr(FilterXExpr *s, FilterXObject *lhs, FilterXObject *cloned)
{
  return _do_nullv_setattr((FilterXSetAttr *) s, lhs, cloned);
}

/* Same as _do_setattr but calls filterx_dict_set_subscript directly, without the
 * ref-setattr → mapping-setattr → set_subscript vtable chain.
 *
 * The static type DICT is only a hint. A coercing container (e.g. otel_logrecord.body keeps an
 * assigned dict as an otel_kvlist) has a non-dict layout at runtime, which the downcast in
 * filterx_dict_set_subscript cannot take. Guard it, and fall back to the vtable setattr.
 *
 * _emit_setattr_call() guards @cloned against NULL, only @lhs can still fail here. */
__attribute__((used))
FilterXObject *
fx_jit_typed_setattr_dict(FilterXExpr *s, FilterXObject *lhs, FilterXObject *cloned)
{
  FilterXSetAttr *self = (FilterXSetAttr *) s;

  if (!lhs)
    {
      filterx_eval_push_error_static_info("Failed to set-attribute to object",
                                          "Failed to evaluate expression");
      goto error;
    }

  if (!filterx_object_is_type_or_ref(lhs, &FILTERX_TYPE_NAME(dict)))
    return _do_setattr(self, lhs, cloned);

  if (!filterx_dict_set_subscript(lhs, self->attr, &cloned))
    {
      filterx_eval_push_error_static_info("Failed to set-attribute to object",
                                          "setattr() method failed");
      goto error;
    }

  filterx_object_unref(lhs);
  return cloned;

error:
  filterx_object_unref(lhs);
  filterx_object_unref(cloned);
  return NULL;
}

__attribute__((used))
FilterXObject *
fx_jit_typed_nullv_setattr_dict(FilterXExpr *s, FilterXObject *lhs, FilterXObject *cloned)
{
  if (filterx_object_extract_null(cloned))
    {
      filterx_object_unref(lhs);
      return cloned;
    }
  return fx_jit_typed_setattr_dict(s, lhs, cloned);
}

static inline FilterXIRValue
_emit_setattr_call(FilterXSetAttr *self, FilterXJIT *jit, const gchar *fn_name, gboolean nullv)
{
  FilterXJITFFI *ffi = filterx_jit_get_ffi(jit);

  FilterXIRShortCircuit short_circuit;
  fx_jit_emit_short_circuit_begin(jit, &short_circuit, "setattr");

  /* NOTE: we need to fork the rhs first, so that the lhs will notice it is
   * shared and can clone accordingly.  This is needed to make sure
   * something like `d.sub = d` works.
   */

  FilterXIRValue rhs = filterx_expr_compile_or_eval(self->new_value, jit);

  /* mirrors _setattr_eval()/_nullv_setattr_eval(): the lhs is not evaluated when the rhs
   * fails. NOTE: a null object right hand side is not an early exit here, the `=??` form
   * evaluates the lhs and drops it in _do_nullv_setattr(). set-subscript differs. */
  if (nullv)
    fx_jit_emit_bail_if_rhs_suppressed(jit, &short_circuit, rhs);
  else
    fx_jit_emit_bail_if_null(jit, &short_circuit, rhs, NULL);

  FilterXIRValue cloned = fx_jit_emit_object_cow_fork2(jit, rhs);
  FilterXIRValue lhs = filterx_expr_compile_or_eval_typed(self->object, jit);

  FilterXIRValue args[] = { fx_jit_emit_const_ptr(jit, self), lhs, cloned };
  FilterXIRType param_tys[] = { ffi->ptr_ty, ffi->ptr_ty, ffi->ptr_ty };
  FilterXIRValue result = fx_jit_emit_extern_call(jit, fn_name, ffi->ptr_ty, param_tys, args, 3);

  return fx_jit_emit_short_circuit_end(jit, &short_circuit, result);
}

FilterXIRValue
_setattr_compile(FilterXExpr *s, FilterXJIT *jit)
{
  FilterXSetAttr *self = (FilterXSetAttr *) s;
  const gchar *fn_name = self->object->static_type == FILTERX_STATIC_TYPE_DICT
                         ? "fx_jit_typed_setattr_dict"
                         : "fx_jit_do_setattr";
  return _emit_setattr_call(self, jit, fn_name, FALSE);
}

FilterXIRValue
_nullv_setattr_compile(FilterXExpr *s, FilterXJIT *jit)
{
  FilterXSetAttr *self = (FilterXSetAttr *) s;
  const gchar *fn_name = self->object->static_type == FILTERX_STATIC_TYPE_DICT
                         ? "fx_jit_typed_nullv_setattr_dict"
                         : "fx_jit_do_nullv_setattr";
  return _emit_setattr_call(self, jit, fn_name, TRUE);
}

#endif
