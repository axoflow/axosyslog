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
#include "filterx/expr-variable.h"
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
      return NULL;
    }

  /* cow_fork2 consumes the ref on value */
  cloned = filterx_object_cow_fork2(value, NULL);

  if (!filterx_expr_assign(self->super.lhs, &cloned))
    {
      filterx_eval_push_error_static_info("Failed to assign value", "assign() method failed");
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

__attribute__((used))
FilterXObject *
fx_jit_nullv_assign_suppress_error(void)
{
  return _suppress_error();
}

static FilterXIRValue
_compile_assign_to_lhs(FilterXAssign *self, FilterXJIT *jit)
{
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
  fx_jit_emit_eval_push_error_static_info(jit, "Failed to assign value", "Failed to evaluate right hand side");
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
_compile_nullv_assign_to_lhs(FilterXAssign *self, FilterXJIT *jit)
{
  FilterXJITFFI *ffi = filterx_jit_get_ffi(jit);
  FilterXIRBuilder ir = filterx_jit_get_ir_builder(jit);
  FilterXIRValue block = filterx_jit_ir_get_current_block(jit);

  FilterXIRValue value = filterx_expr_compile_or_eval(self->super.rhs, jit);

  FilterXIRSequence rhs_error = filterx_jit_ir_create_sequence(jit, "nullv_assign_rhs_error", block);
  FilterXIRSequence check_null = filterx_jit_ir_create_sequence(jit, "nullv_assign_check_null", block);
  FilterXIRSequence assign = filterx_jit_ir_create_sequence(jit, "nullv_assign_value", block);
  FilterXIRSequence finish = filterx_jit_ir_create_sequence(jit, "nullv_assign_finish", block);

  LLVMBuildCondBr(ir, LLVMBuildIsNull(ir, value, "rhs_is_null"), rhs_error, check_null);

  filterx_jit_ir_add_sequence_to_block(jit, rhs_error, block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(jit, rhs_error);
  FilterXIRValue suppressed = fx_jit_emit_extern_call(jit, "fx_jit_nullv_assign_suppress_error",
                                                      ffi->ptr_ty, NULL, NULL, 0);
  LLVMBuildBr(ir, finish);

  filterx_jit_ir_add_sequence_to_block(jit, check_null, block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(jit, check_null);
  FilterXIRType extract_param_tys[] = { ffi->ptr_ty };
  FilterXIRValue extract_args[] = { value };
  FilterXIRValue extracted = fx_jit_emit_extern_call(jit, "fx_jit_object_extract_null",
                                                     ffi->i32_ty, extract_param_tys, extract_args, 1);
  FilterXIRValue rhs_is_null_object = LLVMBuildICmp(ir, LLVMIntNE, extracted,
                                                    LLVMConstInt(ffi->i32_ty, 0, FALSE), "rhs_is_null_object");
  FilterXIRSequence check_null_seq = LLVMGetInsertBlock(ir);
  LLVMBuildCondBr(ir, rhs_is_null_object, finish, assign);

  filterx_jit_ir_add_sequence_to_block(jit, assign, block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(jit, assign);
  FilterXIRValue cloned = fx_jit_emit_object_cow_fork2(jit, value);
  FilterXIRValue assigned = filterx_expr_compile_assign(self->super.lhs, jit, cloned);
  FilterXIRSequence assigned_seq = LLVMGetInsertBlock(ir);
  LLVMBuildBr(ir, finish);

  filterx_jit_ir_add_sequence_to_block(jit, finish, block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(jit, finish);
  FilterXIRValue result = LLVMBuildPhi(ir, ffi->ptr_ty, "nullv_assign_result");
  FilterXIRValue incoming_values[] = { suppressed, value, assigned };
  FilterXIRSequence incoming_blocks[] = { rhs_error, check_null_seq, assigned_seq };
  LLVMAddIncoming(result, incoming_values, incoming_blocks, 3);
  return result;
}

static FilterXIRValue
_nullv_assign_compile(FilterXExpr *s, FilterXJIT *jit)
{
  FilterXAssign *self = (FilterXAssign *) s;

  if (!filterx_expr_can_compile_assign(self->super.lhs))
    return fx_jit_emit_expr_eval(jit, s);

  return _compile_nullv_assign_to_lhs(self, jit);
}

static void
_assign_infer_types(FilterXExpr *s, FilterXTypeEnv *env)
{
  FilterXAssign *self = (FilterXAssign *) s;

  filterx_expr_infer_types(self->super.rhs, env);
  /* LHS may be e.g. setattr / set-subscript; visit it so deep variable reads in the LHS
   * see the env. We do not derive the assign's result type from the LHS's static_type. */
  filterx_expr_infer_types(self->super.lhs, env);

  FilterXStaticTypeSpec rhs_spec = self->super.rhs ? self->super.rhs->static_type : INITIAL_FILTERX_STATIC_TYPE_SPEC;

  FilterXVariableHandle handle;
  if (filterx_variable_expr_get_handle(self->super.lhs, &handle))
    {
      /* Whole-variable overwrite: any per-key type recorded under the old value is stale. */
      filterx_type_env_invalidate_attr_chains(env, handle);
      filterx_type_spec_set(env, handle, rhs_spec);
    }

  s->static_type = rhs_spec;
}

static void
_nullv_assign_infer_types(FilterXExpr *s, FilterXTypeEnv *env)
{
  FilterXAssign *self = (FilterXAssign *) s;

  filterx_expr_infer_types(self->super.rhs, env);
  filterx_expr_infer_types(self->super.lhs, env);

  FilterXStaticTypeSpec rhs_spec = self->super.rhs ? self->super.rhs->static_type : INITIAL_FILTERX_STATIC_TYPE_SPEC;

  /* nullv-assign is a runtime branch: if RHS is null, LHS keeps its prior value untouched
   * (any per-key info recorded for it is still valid); otherwise LHS is wholly replaced by
   * RHS (the old per-key info is now stale). Model both branches on their own env clone,
   * same as if/else, and meet them back together — this both computes meet(prior, rhs) for
   * the handle itself and drops any attr_to_spec entries that don't survive in both branches. */
  FilterXVariableHandle handle;
  if (filterx_variable_expr_get_handle(self->super.lhs, &handle))
    {
      FilterXTypeEnv *assigned_env = filterx_type_env_clone(env);
      filterx_type_env_invalidate_attr_chains(assigned_env, handle);
      filterx_type_spec_set(assigned_env, handle, rhs_spec);

      filterx_type_env_meet_into(env, assigned_env);
      filterx_type_env_free(assigned_env);
    }

  s->static_type = INITIAL_FILTERX_STATIC_TYPE_SPEC;
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
  self->super.super.infer_types = _assign_infer_types;
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
  self->super.super.infer_types = _nullv_assign_infer_types;
  self->super.super.compile = _nullv_assign_compile;
#endif
  return &self->super.super;
}
