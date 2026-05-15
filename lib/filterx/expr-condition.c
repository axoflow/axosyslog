/*
 * Copyright (c) 2023 shifter <shifter@axoflow.com>
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

#include "filterx/expr-condition.h"
#include "filterx/expr-literal.h"
#include "filterx/object-primitive.h"
#include "filterx/filterx-eval.h"
#include "scratch-buffers.h"
#include "stats/stats-registry.h"
#include "stats/stats-cluster-single.h"

typedef struct _FilterXConditional FilterXConditional;

struct _FilterXConditional
{
  FilterXExpr super;
  FilterXExpr *condition;
  FilterXExpr *true_branch;
  FilterXExpr *false_branch;
};

static void
_free(FilterXExpr *s)
{
  FilterXConditional *self = (FilterXConditional *) s;

  filterx_expr_unref(self->condition);
  filterx_expr_unref(self->true_branch);
  filterx_expr_unref(self->false_branch);
  filterx_expr_free_method(s);
}

static gboolean
_condition_is_truthy(FilterXConditional *self, FilterXObject *condition_value)
{
  gboolean truthy = filterx_object_truthy(condition_value);

  if (trace_flag)
    {
      ScratchBuffersMarker mark;
      GString *buf = scratch_buffers_alloc_and_mark(&mark);

      if (!filterx_object_repr(condition_value, buf))
        {
          LogMessageValueType t;
          if (!filterx_object_marshal(condition_value, buf, &t))
            g_assert_not_reached();
        }

      msg_trace(truthy ? "FILTERX CONDT" : "FILTERX CONDF",
                filterx_expr_format_location_tag(self->condition),
                evt_tag_mem("value", buf->str, buf->len),
                evt_tag_int("truthy", truthy),
                evt_tag_str("type", filterx_object_get_type_name(condition_value)));

      scratch_buffers_reclaim_marked(mark);
    }

  return truthy;
}

static FilterXObject *
_eval_conditional(FilterXExpr *s)
{
  FilterXConditional *self = (FilterXConditional *) s;
  FilterXObject *condition_value = filterx_expr_eval(self->condition);
  FilterXObject *result = NULL;

  if (!condition_value)
    {
      filterx_eval_push_error_static_info("Failed to evaluate conditional", s, "Failed to evaluate condition");
      return NULL;
    }

  if (_condition_is_truthy(self, condition_value))
    {
      if (self->true_branch)
        result = filterx_expr_eval(self->true_branch);
      else
        result = filterx_object_ref(condition_value);

      if (!result)
        filterx_eval_push_error_static_info("Failed to evaluate conditional", s, "Failed to evaluate true branch");
    }
  else
    {
      if (self->false_branch)
        result = filterx_expr_eval(self->false_branch);
      else
        result = filterx_boolean_new(TRUE);

      if (!result)
        filterx_eval_push_error_static_info("Failed to evaluate conditional", s, "Failed to evaluate false branch");
    }

  filterx_object_unref(condition_value);
  return result;
}

static FilterXExpr *
_optimize(FilterXExpr *s)
{
  FilterXConditional *self = (FilterXConditional *) s;

  if (!filterx_expr_is_literal(self->condition))
    return NULL;

  FilterXObject *condition_value = filterx_literal_get_value(self->condition);

  g_assert(condition_value);
  gboolean condition_truthy = filterx_object_truthy(condition_value);
  filterx_object_unref(condition_value);

  if (condition_truthy)
    {
      if (self->true_branch)
        return filterx_expr_ref(self->true_branch);
      else
        return filterx_expr_ref(self->condition);
    }
  else
    {
      if (self->false_branch)
        return filterx_expr_ref(self->false_branch);
      else
        return filterx_literal_new(filterx_boolean_new(TRUE));
    }

  return NULL;
}

void
filterx_conditional_set_true_branch(FilterXExpr *s, FilterXExpr *true_branch)
{
  FilterXConditional *self = (FilterXConditional *) s;

  filterx_expr_unref(self->true_branch);
  self->true_branch = true_branch;
}

void
filterx_conditional_set_false_branch(FilterXExpr *s, FilterXExpr *false_branch)
{
  FilterXConditional *self = (FilterXConditional *) s;

  filterx_expr_unref(self->false_branch);
  self->false_branch = false_branch;
}

gboolean
_conditional_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXConditional *self = (FilterXConditional *) s;

  FilterXExpr **exprs[] = { &self->condition, &self->true_branch, &self->false_branch };

  for (gsize i = 0; i < G_N_ELEMENTS(exprs); i++)
    {
      if (!filterx_expr_visit(s, exprs[i], f, user_data))
        return FALSE;
    }

  return TRUE;
}

#if SYSLOG_NG_ENABLE_JIT

#include "filterx/jit/jit.h"
#include "filterx/jit/ffi.h"

__attribute__((used))
gboolean
fx_jit_condition_is_truthy(FilterXConditional *self, FilterXObject *condition_value)
{
  return _condition_is_truthy(self, condition_value);
}

static inline FilterXIRValue
_emit_condition_is_truthy(FilterXJIT *jit, FilterXConditional *self, FilterXIRValue condition_value)
{
  FilterXJITFFI *ffi = filterx_jit_get_ffi(jit);
  return fx_jit_emit_extern_call(jit, "fx_jit_condition_is_truthy", ffi->i32_ty,
                                 (LLVMTypeRef[]){ ffi->ptr_ty, ffi->ptr_ty },
                                 (FilterXIRValue[]){ fx_jit_emit_const_ptr(jit, self), condition_value }, 2);
}

static FilterXIRValue
_compile_conditional(FilterXExpr *s, FilterXJIT *jit)
{
  FilterXConditional *self = (FilterXConditional *) s;
  FilterXJITFFI *ffi = filterx_jit_get_ffi(jit);
  FilterXIRBuilder ir = filterx_jit_get_ir_builder(jit);
  FilterXIRValue block = filterx_jit_ir_get_current_block(jit);

  FilterXIRValue result_slot = LLVMBuildAlloca(ir, ffi->ptr_ty, "result");
  LLVMBuildStore(ir, LLVMConstNull(ffi->ptr_ty), result_slot);

  FilterXIRSequence cond_null = filterx_jit_ir_create_sequence(jit, "conditional_null", block);
  FilterXIRSequence cond_check = filterx_jit_ir_create_sequence(jit, "conditional_check", block);
  FilterXIRSequence true_branch = filterx_jit_ir_create_sequence(jit, "conditional_true", block);
  FilterXIRSequence false_branch = filterx_jit_ir_create_sequence(jit, "conditional_false", block);
  FilterXIRSequence finish = filterx_jit_ir_create_sequence(jit, "conditional_finish", block);

  FilterXIRValue condition_value = filterx_expr_compile_or_eval(self->condition, jit);

  /* if (!condition_value) { push_error; goto finish; } */
  FilterXIRValue is_null = LLVMBuildIsNull(ir, condition_value, "is_null");
  LLVMBuildCondBr(ir, is_null, cond_null, cond_check);
  filterx_jit_ir_add_sequence_to_block(jit, cond_null, block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(jit, cond_null);
  fx_jit_emit_eval_push_error_static_info(jit, "Failed to evaluate conditional", s, "Failed to evaluate condition");
  LLVMBuildBr(ir, finish);

  /* if (_condition_is_truthy(self, condition_value)) */
  filterx_jit_ir_add_sequence_to_block(jit, cond_check, block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(jit, cond_check);
  FilterXIRValue truthy = _emit_condition_is_truthy(jit, self, condition_value);
  FilterXIRValue is_truthy = LLVMBuildICmp(ir, LLVMIntNE, truthy, LLVMConstInt(ffi->i32_ty, 0, FALSE), "is_truthy");
  LLVMBuildCondBr(ir, is_truthy, true_branch, false_branch);

  /* true_branch */
  filterx_jit_ir_add_sequence_to_block(jit, true_branch, block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(jit, true_branch);
  if (self->true_branch)
    {
      fx_jit_emit_object_unref(jit, condition_value);
      LLVMBuildStore(ir, filterx_expr_compile_or_eval(self->true_branch, jit), result_slot);
    }
  else
    LLVMBuildStore(ir, condition_value, result_slot);
  LLVMBuildBr(ir, finish);

  /* false_branch */
  filterx_jit_ir_add_sequence_to_block(jit, false_branch, block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(jit, false_branch);
  fx_jit_emit_object_unref(jit, condition_value);
  if (self->false_branch)
    LLVMBuildStore(ir, filterx_expr_compile_or_eval(self->false_branch, jit), result_slot);
  else
    LLVMBuildStore(ir, fx_jit_emit_boolean_new(jit, TRUE), result_slot);
  LLVMBuildBr(ir, finish);

  /* finish */
  filterx_jit_ir_add_sequence_to_block(jit, finish, block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(jit, finish);
  return LLVMBuildLoad2(ir, ffi->ptr_ty, result_slot, "result");
}

#endif

FilterXExpr *
filterx_conditional_new(FilterXExpr *condition)
{
  FilterXConditional *self = g_new0(FilterXConditional, 1);
  filterx_expr_init_instance(&self->super, "conditional", FXE_READ);
  self->super.eval = _eval_conditional;
  self->super.optimize = _optimize;
  self->super.walk_children = _conditional_walk;
  self->super.free_fn = _free;
#if SYSLOG_NG_ENABLE_JIT
  self->super.compile = _compile_conditional;
#endif
  self->super.suppress_from_trace = TRUE;
  self->condition = condition;
  return &self->super;
}

FilterXExpr *
filterx_conditional_find_tail(FilterXExpr *s)
{
  /* check if this is a FilterXConditional instance */
  if (s->eval != _eval_conditional)
    return NULL;

  FilterXConditional *self = (FilterXConditional *) s;
  if (self->false_branch)
    {
      FilterXConditional *tail = (FilterXConditional *) filterx_conditional_find_tail(self->false_branch);
      if (tail)
        {
          g_assert(tail->false_branch == NULL);
          return &tail->super;
        }
    }
  return s;
}
