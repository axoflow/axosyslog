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
#include "filterx/expr-getattr-private.h"
#include "filterx/expr-getattr-devirt.h"
#include "filterx/filterx-eval.h"

#if SYSLOG_NG_ENABLE_JIT

#include "filterx/jit/jit.h"
#include "filterx/jit/ffi.h"
#include "filterx/object-dict.h"

__attribute__((used))
FilterXObject *
fx_jit_do_getattr(FilterXExpr *s, FilterXObject *variable)
{
  return _do_getattr((FilterXGetAttr *) s, variable);
}

/* dict.attr is dict[attr] with a known-string key, so this calls filterx_dict_get_subscript
 * directly, without the mapping's getattr → get_subscript hop. self->attr is borrowed from
 * the FilterXGetAttr struct and must not be unrefed.
 *
 * filterx_dict_get_subscript unwraps @variable read-only, so a ref also needs an explicit
 * float of the shared child to keep copy-on-write. */
__attribute__((used))
FilterXObject *
fx_jit_typed_getattr_dict(FilterXExpr *s, FilterXObject *variable)
{
  FilterXGetAttr *self = (FilterXGetAttr *) s;

  if (!variable)
    {
      filterx_eval_push_error_static_info("Failed to get-attribute from object",
                                          "Failed to evaluate expression");
      return NULL;
    }

  FilterXObject *result = filterx_dict_get_subscript(variable, self->attr);
  if (!result)
    filterx_eval_push_error_static_info("Failed to get-attribute from object",
                                        "Failed to evaluate key");
  else if (filterx_object_is_ref(variable))
    result = filterx_ref_replace_shared_xref_with_a_shadow(result, variable);

  filterx_object_unref(variable);
  return result;
}

static inline FilterXIRValue
_emit_getattr_call(FilterXGetAttr *self, FilterXJIT *jit, const gchar *fn_name)
{
  FilterXJITFFI *ffi = filterx_jit_get_ffi(jit);

  FilterXIRValue variable = filterx_expr_compile_or_eval_typed(self->operand, jit);

  FilterXIRValue args[] = { fx_jit_emit_const_ptr(jit, self), variable };
  FilterXIRType param_tys[] = { ffi->ptr_ty, ffi->ptr_ty };
  return fx_jit_emit_extern_call(jit, fn_name, ffi->ptr_ty, param_tys, args, 2);
}

FilterXIRValue
_getattr_compile(FilterXExpr *s, FilterXJIT *jit)
{
  FilterXGetAttr *self = (FilterXGetAttr *) s;
  const gchar *fn_name = self->operand->static_type == FILTERX_STATIC_TYPE_DICT
                         ? "fx_jit_typed_getattr_dict"
                         : "fx_jit_do_getattr";
  return _emit_getattr_call(self, jit, fn_name);
}

#endif
