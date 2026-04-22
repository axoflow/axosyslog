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

/* De-inlined version of FilterX functions */
/* TODO: remove these once FilterX JIT is stable: non-inline calls should replace the inlined versions */

__attribute__((used))
FilterXObject *
fx_jit_expr_eval(FilterXExpr *self)
{
  return filterx_expr_eval(self);
}

__attribute__((used))
FilterXObject *
fx_jit_expr_eval_typed(FilterXExpr *self)
{
  return filterx_expr_eval_typed(self);
}

__attribute__((used))
FilterXObject *
fx_jit_object_ref(FilterXObject *self)
{
  return filterx_object_ref(self);
}

__attribute__((used))
FilterXObject *
fx_jit_object_cow_fork2(FilterXObject *self)
{
  if (!self)
    return NULL;
  return filterx_object_cow_fork2(self, NULL);
}

__attribute__((used))
void
fx_jit_object_unref(FilterXObject *self)
{
  filterx_object_unref(self);
}

__attribute__((used))
gboolean
fx_jit_object_truthy(FilterXObject *self)
{
  return filterx_object_truthy(self);
}

__attribute__((used))
void
fx_jit_eval_context_make_writable(FilterXEvalContext *context)
{
  filterx_eval_context_make_writable(context);
}

__attribute__((used))
FilterXObject *
fx_jit_boolean_new(gboolean value)
{
  return filterx_boolean_new(value);
}

/* FFI */

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

  jit->ffi.expr_eval = _declare_func(mod, "fx_jit_expr_eval", ptr, (LLVMTypeRef[]){ ptr }, 1);
  jit->ffi.expr_eval_typed = _declare_func(mod, "fx_jit_expr_eval_typed", ptr, (LLVMTypeRef[]){ ptr }, 1);
  jit->ffi.expr_make_typed_object = _declare_func(mod, "fx_jit_expr_make_typed_object", ptr,
                                                  (LLVMTypeRef[]){ ptr, ptr }, 2);

  jit->ffi.object_ref = _declare_func(mod, "fx_jit_object_ref", ptr, (LLVMTypeRef[]){ ptr }, 1);
  jit->ffi.object_unref = _declare_func(mod, "fx_jit_object_unref", v, (LLVMTypeRef[]){ ptr }, 1);
  jit->ffi.object_cow_fork2 = _declare_func(mod, "fx_jit_object_cow_fork2", ptr, (LLVMTypeRef[]){ ptr }, 1);
  jit->ffi.object_truthy = _declare_func(mod, "fx_jit_object_truthy", i32, (LLVMTypeRef[]){ ptr }, 1);
  jit->ffi.boolean_new = _declare_func(mod, "fx_jit_boolean_new", ptr, (LLVMTypeRef[]){ i32 }, 1);

  jit->ffi.eval_context_make_writable = _declare_func(mod, "fx_jit_eval_context_make_writable", v,
                                                      (LLVMTypeRef[]){ ptr }, 1);

  jit->ffi.eval_push_error = _declare_func(mod, "filterx_eval_push_error", v,
                                           (LLVMTypeRef[]){ ptr, ptr, ptr }, 3);
  jit->ffi.eval_push_falsy_error = _declare_func(mod, "filterx_eval_push_falsy_error", v,
                                                 (LLVMTypeRef[]){ ptr, ptr, ptr }, 3);
  jit->ffi.eval_push_error_static_info = _declare_func(mod, "filterx_eval_push_error_static_info", v,
                                                       (LLVMTypeRef[]){ ptr, ptr, ptr }, 3);

  jit->ffi.eval_push_error_info_printf.ty = LLVMFunctionType(v, (LLVMTypeRef[]){ ptr, ptr, ptr }, 3, TRUE);
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
  return _emit_call(jit, jit->ffi.boolean_new, (FilterXIRValue[]){ LLVMConstInt(ffi->i32_ty, value, FALSE) }, 1);
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
fx_jit_emit_eval_context_make_writable(FilterXJIT *jit, FilterXIRValue eval_ctx)
{
  _emit_call(jit, jit->ffi.eval_context_make_writable, &eval_ctx, 1);
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

#endif
