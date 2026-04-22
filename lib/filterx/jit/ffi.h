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

#ifndef FILTERX_JIT_FFI_H
#define FILTERX_JIT_FFI_H

#include "syslog-ng.h"
#include "filterx/jit/jit.h"

typedef struct _FFICall
{
  FilterXIRType ty;
  FilterXIRValue fn;
} FilterXJITFFICall;

/*
 * The most frequently used FilterX calls are cached in FilterXJITFFI
 * and have their dedicated fx_jit_emit_*() call.
 *
 * fx_jit_emit_extern_call() can be used for everything else.
 */
typedef struct _FilterXJITFFI
{
  FilterXIRType ptr_ty;
  FilterXIRType i32_ty;
  FilterXIRType i64_ty;
  FilterXIRType void_ty;

  FilterXJITFFICall expr_eval;
  FilterXJITFFICall expr_eval_typed;
  FilterXJITFFICall expr_make_typed_object;

  FilterXJITFFICall object_ref;
  FilterXJITFFICall object_unref;
  FilterXJITFFICall object_cow_fork2;
  FilterXJITFFICall object_truthy;
  FilterXJITFFICall boolean_new;

  FilterXJITFFICall eval_context_make_writable;

  FilterXJITFFICall eval_push_error;
  FilterXJITFFICall eval_push_falsy_error;
  FilterXJITFFICall eval_push_error_static_info;
  FilterXJITFFICall eval_push_error_info_printf;
} FilterXJITFFI;


FilterXJITFFI *filterx_jit_get_ffi(FilterXJIT *self);

/* TODO partialJIT: remove once all expressions implement compile() */
FilterXIRValue fx_jit_emit_expr_eval(FilterXJIT *jit, FilterXExpr *expr);
FilterXIRValue fx_jit_emit_expr_eval_typed(FilterXJIT *jit, FilterXExpr *expr);

FilterXIRValue fx_jit_emit_object_ref(FilterXJIT *jit, FilterXIRValue obj);
void fx_jit_emit_object_unref(FilterXJIT *jit, FilterXIRValue obj);
FilterXIRValue fx_jit_emit_object_cow_fork2(FilterXJIT *jit, FilterXIRValue obj);
FilterXIRValue fx_jit_emit_object_truthy(FilterXJIT *jit, FilterXIRValue obj);
FilterXIRValue fx_jit_emit_boolean_new(FilterXJIT *jit, gboolean value);

FilterXIRValue fx_jit_emit_const_ptr(FilterXJIT *jit, gconstpointer p);

void fx_jit_emit_eval_context_make_writable(FilterXJIT *jit, FilterXIRValue eval_ctx);

void fx_jit_emit_eval_push_error(FilterXJIT *jit, const gchar *msg, FilterXExpr *expr, FilterXIRValue obj);
void fx_jit_emit_eval_push_falsy_error(FilterXJIT *jit, const gchar *msg, FilterXExpr *expr, FilterXIRValue obj);
void fx_jit_emit_eval_push_error_static_info(FilterXJIT *jit, const gchar *msg, FilterXExpr *expr, const gchar *info);
void fx_jit_emit_eval_push_error_info_printf(FilterXJIT *jit, const gchar *msg, FilterXExpr *expr,
                                             const gchar *fmt, ...)  G_GNUC_PRINTF(4, 5);


FilterXIRValue fx_jit_emit_extern_call(FilterXJIT *jit, const gchar *name, FilterXIRType return_ty,
                                       FilterXIRType *param_tys, FilterXIRValue *args, unsigned param_count);

/* private */
void filterx_jit_ffi_init(FilterXJIT *jit);

#endif
