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

  FilterXJITFFICall eval_push_error;
  FilterXJITFFICall eval_push_falsy_error;
  FilterXJITFFICall eval_push_error_static_info;
  FilterXJITFFICall eval_push_error_info_printf;
} FilterXJITFFI;


FilterXJITFFI *filterx_jit_get_ffi(FilterXJIT *self);

/* TODO partialJIT: remove once all expressions implement compile() */
FilterXIRValue fx_jit_emit_expr_eval(FilterXJIT *jit, FilterXExpr *expr);
FilterXIRValue fx_jit_emit_expr_eval_typed(FilterXJIT *jit, FilterXExpr *expr);
FilterXIRValue fx_jit_emit_expr_make_typed_object(FilterXJIT *jit, FilterXExpr *expr, FilterXIRValue obj);
FilterXIRValue fx_jit_emit_expr_propagate_to_error_if_null(FilterXJIT *jit, FilterXExpr *expr, FilterXIRValue result);

FilterXIRValue fx_jit_emit_object_ref(FilterXJIT *jit, FilterXIRValue obj);
void fx_jit_emit_object_unref(FilterXJIT *jit, FilterXIRValue obj);
FilterXIRValue fx_jit_emit_object_cow_fork2(FilterXJIT *jit, FilterXIRValue obj);
FilterXIRValue fx_jit_emit_object_truthy(FilterXJIT *jit, FilterXIRValue obj);
FilterXIRValue fx_jit_emit_boolean_new(FilterXJIT *jit, gboolean value);

FilterXIRValue fx_jit_emit_const_ptr(FilterXJIT *jit, gconstpointer p);

/*
 * Short-circuit region. The interpreter stops evaluating the operands of an expression as
 * soon as one of them fails, and the emitted code has to do the same, or the skipped
 * operands run their side effects. Everything emitted between the begin and the end call
 * runs only while no guarded value bails out.
 *
 *   FilterXIRShortCircuit sc;
 *   fx_jit_emit_short_circuit_begin(jit, &sc, "setattr");
 *   FilterXIRValue rhs = filterx_expr_compile_or_eval(self->new_value, jit);
 *   fx_jit_emit_bail_if_null(jit, &sc, rhs, NULL);
 *   FilterXIRValue lhs = filterx_expr_compile_or_eval_typed(self->object, jit);
 *   FilterXIRValue result = fx_jit_emit_extern_call(...);
 *   return fx_jit_emit_short_circuit_end(jit, &sc, result);
 */
typedef struct _FilterXIRShortCircuit
{
  const gchar *name;
  guint num_bails;
  FilterXIRValue block;
  FilterXIRValue result_slot;
  FilterXIRSequence finish;
} FilterXIRShortCircuit;

void fx_jit_emit_short_circuit_begin(FilterXJIT *jit, FilterXIRShortCircuit *self, const gchar *name);
FilterXIRValue fx_jit_emit_short_circuit_end(FilterXJIT *jit, FilterXIRShortCircuit *self, FilterXIRValue result);

/* The expression fails: @value is NULL because its expression failed and already pushed its
 * own error. @release is an object the skipped code would have consumed, or NULL. */
void fx_jit_emit_bail_if_null(FilterXJIT *jit, FilterXIRShortCircuit *self, FilterXIRValue value,
                              FilterXIRValue release);

/* The `=??` forms: a failed right hand side is suppressed and becomes a null object. */
void fx_jit_emit_bail_if_rhs_suppressed(FilterXJIT *jit, FilterXIRShortCircuit *self, FilterXIRValue value);

/* The `=??` forms: a right hand side that is a null object is the result, and nothing is
 * written. @release is an object the skipped code would have consumed, or NULL. */
void fx_jit_emit_bail_if_rhs_null_object(FilterXJIT *jit, FilterXIRShortCircuit *self, FilterXIRValue value,
                                         FilterXIRValue release);

void fx_jit_emit_eval_push_error(FilterXJIT *jit, const gchar *msg, FilterXIRValue obj);
void fx_jit_emit_eval_push_falsy_error(FilterXJIT *jit, const gchar *msg, FilterXIRValue obj);
void fx_jit_emit_eval_push_error_static_info(FilterXJIT *jit, const gchar *msg, const gchar *info);
void fx_jit_emit_eval_push_error_info_printf(FilterXJIT *jit, const gchar *msg, const gchar *fmt, ...)
G_GNUC_PRINTF(3, 4);


FilterXIRValue fx_jit_emit_extern_call(FilterXJIT *jit, const gchar *name, FilterXIRType return_ty,
                                       FilterXIRType *param_tys, FilterXIRValue *args, unsigned param_count);

/* private */
void filterx_jit_ffi_init(FilterXJIT *jit);

#endif
