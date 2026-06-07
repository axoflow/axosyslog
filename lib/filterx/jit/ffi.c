/*
 * Copyright (c) 2025-2026 László Várady
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

#include "filterx/jit/ffi.h"
#include "filterx/jit/jit.h"
#include "filterx/jit/jit-private.h"

#include "filterx/filterx-expr.h"
#include "filterx/filterx-object.h"
#include "filterx/filterx-eval.h"
#include "filterx/object-primitive.h"

#include <stdarg.h>

#if SYSLOG_NG_ENABLE_JIT

FilterXJITFFI *
filterx_jit_get_ffi(FilterXJIT *self)
{
  return &self->ffi;
}

static inline FilterXJITFFICall
_declare_func(LLVMModuleRef mod, const gchar *name, LLVMTypeRef ret_ty,
              LLVMTypeRef *param_tys, unsigned nparams)
{
  FilterXJITFFICall c;
  c.ty = LLVMFunctionType(ret_ty, param_tys, nparams, FALSE);
  c.fn = LLVMAddFunction(mod, name, c.ty);
  return c;
}

void
filterx_jit_ffi_init(FilterXJIT *jit)
{
  LLVMContextRef ctx = jit->ctx;
  LLVMModuleRef mod = jit->mod;

  LLVMTypeRef ptr = jit->ffi.ptr_ty = LLVMPointerTypeInContext(ctx, 0);
  LLVMTypeRef i32 = jit->ffi.i32_ty = LLVMInt32TypeInContext(ctx);
  jit->ffi.i64_ty = LLVMInt64TypeInContext(ctx);
  LLVMTypeRef v = jit->ffi.void_ty = LLVMVoidTypeInContext(ctx);

  LLVMTypeRef p1[] = { ptr };
  LLVMTypeRef p2[] = { ptr, ptr };
  LLVMTypeRef p3[] = { ptr, ptr, ptr };
  LLVMTypeRef i1[] = { i32 };

  jit->ffi.expr_eval = _declare_func(mod, "fx_jit_expr_eval", ptr, p1, 1);
  jit->ffi.expr_eval_typed = _declare_func(mod, "fx_jit_expr_eval_typed", ptr, p1, 1);
  jit->ffi.expr_make_typed_object = _declare_func(mod, "fx_jit_expr_make_typed_object", ptr, p2, 2);

  jit->ffi.object_ref = _declare_func(mod, "fx_jit_object_ref", ptr, p1, 1);
  jit->ffi.object_unref = _declare_func(mod, "fx_jit_object_unref", v, p1, 1);
  jit->ffi.object_cow_fork2 = _declare_func(mod, "fx_jit_object_cow_fork2", ptr, p1, 1);
  jit->ffi.object_truthy = _declare_func(mod, "fx_jit_object_truthy", i32, p1, 1);
  jit->ffi.boolean_new = _declare_func(mod, "fx_jit_boolean_new", ptr, i1, 1);

  jit->ffi.eval_push_error = _declare_func(mod, "filterx_eval_push_error", v, p3, 3);
  jit->ffi.eval_push_falsy_error = _declare_func(mod, "filterx_eval_push_falsy_error", v, p3, 3);
  jit->ffi.eval_push_error_static_info = _declare_func(mod, "filterx_eval_push_error_static_info", v, p3, 3);

  jit->ffi.eval_push_error_info_printf.ty = LLVMFunctionType(v, p3, 3, TRUE);
  jit->ffi.eval_push_error_info_printf.fn = LLVMAddFunction(mod, "filterx_eval_push_error_info_printf",
                                                            jit->ffi.eval_push_error_info_printf.ty);
}

static inline FilterXIRValue
_emit_call(FilterXJIT *jit, FilterXJITFFICall c, FilterXIRValue *args, unsigned param_count)
{
  return LLVMBuildCall2(jit->ir, c.ty, c.fn, args, param_count, "");
}

FilterXIRValue
fx_jit_emit_const_ptr(FilterXJIT *jit, gconstpointer p)
{
  LLVMTypeRef ptr_sized_int = LLVMIntTypeInContext(jit->ctx, sizeof(gconstpointer) * 8);
  return LLVMConstIntToPtr(LLVMConstInt(ptr_sized_int, (guintptr) p, FALSE), jit->ffi.ptr_ty);
}

FilterXIRValue
fx_jit_emit_expr_eval(FilterXJIT *jit, FilterXExpr *expr)
{
  LLVMValueRef args[] = { fx_jit_emit_const_ptr(jit, expr) };
  return _emit_call(jit, jit->ffi.expr_eval, args, 1);
}

FilterXIRValue
fx_jit_emit_expr_eval_typed(FilterXJIT *jit, FilterXExpr *expr)
{
  LLVMValueRef args[] = { fx_jit_emit_const_ptr(jit, expr) };
  return _emit_call(jit, jit->ffi.expr_eval_typed, args, 1);
}

FilterXIRValue
fx_jit_emit_expr_make_typed_object(FilterXJIT *jit, FilterXExpr *expr, FilterXIRValue obj)
{
  LLVMValueRef args[] = { fx_jit_emit_const_ptr(jit, expr), obj };
  return _emit_call(jit, jit->ffi.expr_make_typed_object, args, 2);
}

FilterXIRValue
fx_jit_emit_expr_propagate_to_error_if_null(FilterXJIT *jit, FilterXExpr *expr, FilterXIRValue result)
{
  FilterXJITFFI *ffi = filterx_jit_get_ffi(jit);
  FilterXIRBuilder ir = filterx_jit_get_ir_builder(jit);
  FilterXIRValue block = filterx_jit_ir_get_current_block(jit);

  FilterXIRSequence propagate_seq = filterx_jit_ir_create_sequence(jit, "propagate_error", block);
  FilterXIRSequence continue_seq = filterx_jit_ir_create_sequence(jit, "continue", block);

  FilterXIRValue is_null = LLVMBuildIsNull(ir, result, "is_null");
  LLVMBuildCondBr(ir, is_null, propagate_seq, continue_seq);

  filterx_jit_ir_add_sequence_to_block(jit, propagate_seq, block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(jit, propagate_seq);
  FilterXIRType param_tys[] = { ffi->ptr_ty };
  FilterXIRValue args[] = { fx_jit_emit_const_ptr(jit, expr) };
  fx_jit_emit_extern_call(jit, "filterx_eval_update_error_location_from_expr", ffi->void_ty, param_tys, args, 1);
  LLVMBuildBr(ir, continue_seq);

  filterx_jit_ir_add_sequence_to_block(jit, continue_seq, block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(jit, continue_seq);
  return result;
}

FilterXIRValue
fx_jit_emit_object_ref(FilterXJIT *jit, LLVMValueRef obj)
{
  return _emit_call(jit, jit->ffi.object_ref, &obj, 1);
}

void
fx_jit_emit_object_unref(FilterXJIT *jit, LLVMValueRef obj)
{
  _emit_call(jit, jit->ffi.object_unref, &obj, 1);
}

FilterXIRValue
fx_jit_emit_object_cow_fork2(FilterXJIT *jit, LLVMValueRef obj)
{
  return _emit_call(jit, jit->ffi.object_cow_fork2, &obj, 1);
}

FilterXIRValue
fx_jit_emit_object_truthy(FilterXJIT *jit, LLVMValueRef obj)
{
  return _emit_call(jit, jit->ffi.object_truthy, &obj, 1);
}

FilterXIRValue
fx_jit_emit_boolean_new(FilterXJIT *jit, gboolean value)
{
  FilterXJITFFI *ffi = filterx_jit_get_ffi(jit);
  FilterXIRValue args[] = { LLVMConstInt(ffi->i32_ty, value, FALSE) };
  return _emit_call(jit, jit->ffi.boolean_new, args, 1);
}

void
fx_jit_emit_eval_push_falsy_error(FilterXJIT *jit, const gchar *msg, FilterXExpr *expr, FilterXIRValue obj)
{
  LLVMValueRef args[] = { fx_jit_emit_const_ptr(jit, msg), fx_jit_emit_const_ptr(jit, expr), obj };
  _emit_call(jit, jit->ffi.eval_push_falsy_error, args, 3);
}

void
fx_jit_emit_eval_push_error(FilterXJIT *jit, const gchar *msg, FilterXExpr *expr, FilterXIRValue obj)
{
  FilterXIRValue args[] = { fx_jit_emit_const_ptr(jit, msg), fx_jit_emit_const_ptr(jit, expr), obj };
  _emit_call(jit, jit->ffi.eval_push_error, args, 3);
}

void
fx_jit_emit_eval_push_error_static_info(FilterXJIT *jit, const gchar *msg, FilterXExpr *expr, const gchar *info)
{
  FilterXIRValue args[] =
  {
    fx_jit_emit_const_ptr(jit, msg),
    fx_jit_emit_const_ptr(jit, expr),
    fx_jit_emit_const_ptr(jit, info),
  };
  _emit_call(jit, jit->ffi.eval_push_error_static_info, args, 3);
}

void
fx_jit_emit_eval_push_error_info_printf(FilterXJIT *jit, const gchar *msg, FilterXExpr *expr,
                                        const gchar *fmt, ...)
{
  GArray *call_args = g_array_new(FALSE, FALSE, sizeof(FilterXIRValue));


  FilterXIRValue arg = fx_jit_emit_const_ptr(jit, msg);
  g_array_append_val(call_args, arg);

  arg = fx_jit_emit_const_ptr(jit, expr);
  g_array_append_val(call_args, arg);

  arg = fx_jit_emit_const_ptr(jit, fmt);
  g_array_append_val(call_args, arg);

  va_list ap;
  va_start(ap, fmt);
  while ((arg = va_arg(ap, FilterXIRValue)) != NULL)
    g_array_append_val(call_args, arg);
  va_end(ap);

  _emit_call(jit, jit->ffi.eval_push_error_info_printf, (FilterXIRValue *) call_args->data, call_args->len);
  g_array_free(call_args, TRUE);
}

FilterXIRValue
fx_jit_emit_extern_call(FilterXJIT *jit, const gchar *name, FilterXIRType return_ty,
                        FilterXIRType *param_tys, FilterXIRValue *args, unsigned param_count)
{
  FilterXJITFFICall call = {0};

  LLVMValueRef fn = LLVMGetNamedFunction(jit->mod, name);
  LLVMTypeRef fn_ty;
  if (fn)
    {
      fn_ty = LLVMGlobalGetValueType(fn);
      call.ty = fn_ty;
      call.fn = fn;
    }
  else
    call = _declare_func(jit->mod, name, return_ty, param_tys, param_count);

  return _emit_call(jit, call, args, param_count);
}

FilterXObject *fx_jit_attribute_template(FilterXEvalContext *ctx);

__attribute__((used, noinline))
FilterXObject *
fx_jit_attribute_template_opaque(FilterXEvalContext *ctx)
{
  return NULL;
}

void *fx_jit_used_symbols[] =
{
  fx_jit_attribute_template,
  fx_jit_attribute_template_opaque,
};

#endif
