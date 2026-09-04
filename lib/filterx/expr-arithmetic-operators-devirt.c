/*
 * Copyright (c) 2025 Tamas Kosztyu <tamas.kosztyu@axoflow.com>
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
#include "expr-arithmetic-operators-private.h"
#include "expr-arithmetic-operators-devirt.h"
#include "filterx/object-primitive.h"
#include "filterx/object-string.h"
#include "filterx/object-extractor.h"
#include "filterx/filterx-eval.h"

#include <math.h>

#if SYSLOG_NG_ENABLE_JIT

#include "filterx/jit/jit.h"
#include "filterx/jit/ffi.h"

FilterXIRValue
_compile_plus(FilterXExpr *s, FilterXJIT *jit)
{
  if (s->static_type != FILTERX_STATIC_TYPE_STRING)
    return _compile_binary_arithmetic(s, jit, "fx_jit_arithmetic_plus");

  FilterXArithmeticOperator *self = (FilterXArithmeticOperator *) s;
  FilterXJITFFI *ffi = filterx_jit_get_ffi(jit);
  FilterXIRBuilder ir = filterx_jit_get_ir_builder(jit);
  FilterXIRValue block = filterx_jit_ir_get_current_block(jit);

  FilterXIRValue result_slot = filterx_jit_ir_add_stack_slot(jit, ffi->ptr_ty, "result");
  LLVMBuildStore(ir, LLVMConstNull(ffi->ptr_ty), result_slot);

  FilterXIRSequence lhs_null = filterx_jit_ir_create_sequence(jit, "plus_lhs_null", block);
  FilterXIRSequence eval_rhs = filterx_jit_ir_create_sequence(jit, "plus_eval_rhs", block);
  FilterXIRSequence finish = filterx_jit_ir_create_sequence(jit, "plus_finish", block);

  /* Mirrors _eval_op(): only the lhs is evaluated typed, which is what lets the fast path
   * rely on its static type, and the rhs is not evaluated when the lhs fails. */
  FilterXIRValue lhs = self->literal_lhs
                       ? fx_jit_emit_object_ref(jit, fx_jit_emit_const_ptr(jit, self->literal_lhs))
                       : filterx_expr_compile_or_eval_typed(self->super.lhs, jit);

  /* if (!lhs) goto finish; */
  LLVMBuildCondBr(ir, LLVMBuildIsNull(ir, lhs, "lhs_is_null"), lhs_null, eval_rhs);

  filterx_jit_ir_add_sequence_to_block(jit, lhs_null, block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(jit, lhs_null);
  LLVMBuildBr(ir, finish);

  filterx_jit_ir_add_sequence_to_block(jit, eval_rhs, block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(jit, eval_rhs);
  FilterXIRValue rhs = self->literal_rhs
                       ? fx_jit_emit_object_ref(jit, fx_jit_emit_const_ptr(jit, self->literal_rhs))
                       : filterx_expr_compile_or_eval(self->super.rhs, jit);

  LLVMBuildStore(ir, filterx_string_concat_compile(jit, lhs, rhs, s), result_slot);
  LLVMBuildBr(ir, finish);

  filterx_jit_ir_add_sequence_to_block(jit, finish, block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(jit, finish);
  return LLVMBuildLoad2(ir, ffi->ptr_ty, result_slot, "result");
}

/* @lhs is a FilterXString, the eval_typed result of a STRING-static_type operand.
 * _compile_plus() guards it against NULL, only @rhs can still fail here. */
__attribute__((used))
FilterXObject *
fx_jit_string_plus(FilterXObject *lhs, FilterXObject *rhs, FilterXExpr *expr)
{
  FilterXObject *result = NULL;
  if (!rhs)
    {
      filterx_eval_push_error_static_info("Failed to add values", "Failed to evaluate right hand side");
      goto exit;
    }
  result = filterx_string_concat(lhs, rhs);
  if (!result)
    filterx_eval_push_error_static_info("Failed to add values", "add() method failed");

exit:
  filterx_object_unref(lhs);
  filterx_object_unref(rhs);
  return result;
}

FilterXIRValue
filterx_string_concat_compile(FilterXJIT *jit, FilterXIRValue lhs, FilterXIRValue rhs, FilterXExpr *expr)
{
  FilterXJITFFI *ffi = filterx_jit_get_ffi(jit);

  FilterXIRValue args[] = { lhs, rhs, fx_jit_emit_const_ptr(jit, expr) };
  FilterXIRType param_tys[] = { ffi->ptr_ty, ffi->ptr_ty, ffi->ptr_ty };
  return fx_jit_emit_extern_call(jit, "fx_jit_string_plus", ffi->ptr_ty, param_tys, args, 3);
}

#endif
