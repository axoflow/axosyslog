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
#include "filterx/expr-assign.h"
#include "filterx/object-primitive.h"
#include "filterx/filterx-eval.h"
#include "filterx/object-null.h"
#include "filterx/object-message-value.h"
#include "filterx/object-extractor.h"
#include "scratch-buffers.h"

typedef struct FilterXAssign
{
  FilterXBinaryOp super;
} FilterXAssign;

static FilterXObject *
_do_assign(FilterXAssign *self, FilterXObject *value)
{
  FilterXObject *cloned = NULL;

  if (!value)
    {
      filterx_eval_push_error_static_info("Failed to assign value", &self->super.super,
                                          "Failed to evaluate right hand side");
      return NULL;
    }

  /* cow_fork2 consumes the ref on value */
  cloned = filterx_object_cow_fork2(value, NULL);

  if (!filterx_expr_assign(self->super.lhs, &cloned))
    {
      filterx_eval_push_error_static_info("Failed to assign value", &self->super.super,
                                          "assign() method failed");
      filterx_object_unref(cloned);
      return NULL;
    }

  return cloned;
}

static FilterXObject *
_assign_eval(FilterXExpr *s)
{
  FilterXAssign *self = (FilterXAssign *) s;
  return _do_assign(self, filterx_expr_eval(self->super.rhs));
}

static inline FilterXObject *
_suppress_error(void)
{
  filterx_eval_dump_errors("FilterX: null coalesce assignment suppressing error");

  return filterx_null_new();
}

static FilterXObject *
_do_nullv_assign(FilterXAssign *self, FilterXObject *value)
{
  if (!value)
    return _suppress_error();

  if (filterx_object_extract_null(value))
    return value;

  return _do_assign(self, value);
}

static FilterXObject *
_nullv_assign_eval(FilterXExpr *s)
{
  FilterXAssign *self = (FilterXAssign *) s;
  return _do_nullv_assign(self, filterx_expr_eval(self->super.rhs));
}

#if SYSLOG_NG_ENABLE_JIT

#include "filterx/jit/jit.h"
#include "filterx/jit/ffi.h"

static FilterXIRValue
_compile_assign_to_lhs(FilterXAssign *self, FilterXJIT *jit)
{
  FilterXExpr *s = &self->super.super;
  FilterXJITFFI *ffi = filterx_jit_get_ffi(jit);
  FilterXIRBuilder ir = filterx_jit_get_ir_builder(jit);
  FilterXIRValue block = filterx_jit_ir_get_current_block(jit);

  FilterXIRValue value = filterx_expr_compile_or_eval(self->super.rhs, jit);

  FilterXIRSequence rhs_null = filterx_jit_ir_create_sequence(jit, "assign_rhs_null", block);
  FilterXIRSequence assign = filterx_jit_ir_create_sequence(jit, "assign_value", block);
  FilterXIRSequence finish = filterx_jit_ir_create_sequence(jit, "assign_finish", block);

  LLVMBuildCondBr(ir, LLVMBuildIsNull(ir, value, "rhs_is_null"), rhs_null, assign);

  filterx_jit_ir_add_sequence_to_block(jit, rhs_null, block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(jit, rhs_null);
  fx_jit_emit_eval_push_error_static_info(jit, "Failed to assign value", s, "Failed to evaluate right hand side");
  LLVMBuildBr(ir, finish);

  filterx_jit_ir_add_sequence_to_block(jit, assign, block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(jit, assign);
  FilterXIRValue cloned = fx_jit_emit_object_cow_fork2(jit, value);
  FilterXIRValue assigned = filterx_expr_compile_assign(self->super.lhs, jit, cloned);
  FilterXIRSequence assigned_seq = LLVMGetInsertBlock(ir);
  LLVMBuildBr(ir, finish);

  filterx_jit_ir_add_sequence_to_block(jit, finish, block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(jit, finish);
  FilterXIRValue result = LLVMBuildPhi(ir, ffi->ptr_ty, "assign_result");
  FilterXIRValue incoming_values[] = { LLVMConstNull(ffi->ptr_ty), assigned };
  FilterXIRSequence incoming_blocks[] = { rhs_null, assigned_seq };
  LLVMAddIncoming(result, incoming_values, incoming_blocks, 2);
  return result;
}

static FilterXIRValue
_assign_compile(FilterXExpr *s, FilterXJIT *jit)
{
  FilterXAssign *self = (FilterXAssign *) s;

  if (!filterx_expr_can_compile_assign(self->super.lhs))
    return fx_jit_emit_expr_eval(jit, s);

  return _compile_assign_to_lhs(self, jit);
}

static FilterXIRValue
_nullv_assign_compile(FilterXExpr *s, FilterXJIT *jit)
{
  return fx_jit_emit_expr_eval(jit, s);
}

#endif

static void
filterx_assign_init_instance(FilterXAssign *self, const gchar *type,
                             FilterXExpr *lhs, FilterXExpr *rhs)
{
  filterx_binary_op_init_instance(&self->super, type, FXE_WRITE, lhs, rhs);
  self->super.super.ignore_falsy_result = TRUE;
}

/* NOTE: takes the object reference */
FilterXExpr *
filterx_assign_new(FilterXExpr *lhs, FilterXExpr *rhs)
{
  FilterXAssign *self = g_new0(FilterXAssign, 1);

  filterx_assign_init_instance(self, "assign", lhs, rhs);
  self->super.super.eval = _assign_eval;
#if SYSLOG_NG_ENABLE_JIT
  self->super.super.compile = _assign_compile;
#endif
  return &self->super.super;
}

FilterXExpr *
filterx_nullv_assign_new(FilterXExpr *lhs, FilterXExpr *rhs)
{
  FilterXAssign *self = g_new0(FilterXAssign, 1);

  filterx_assign_init_instance(self, "nullv-assign", lhs, rhs);
  self->super.super.eval = _nullv_assign_eval;
#if SYSLOG_NG_ENABLE_JIT
  self->super.super.compile = _nullv_assign_compile;
#endif
  return &self->super.super;
}
