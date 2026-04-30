/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 Attila Szakacs <attila.szakacs@axoflow.com>
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

#include "filterx/expr-compound.h"
#include "filterx/filterx-plist.h"
#include "filterx/filterx-eval.h"
#include "filterx/object-primitive.h"
#include "scratch-buffers.h"
#include "stats/stats-registry.h"
#include "stats/stats-cluster-single.h"

#include <stdarg.h>

typedef struct _FilterXCompoundExpr
{
  FilterXExpr super;
  FilterXExprList exprs;
  /* whether this is a statement expression */
  gboolean return_value_of_last_expr;
} FilterXCompoundExpr;

static inline gboolean
_process_expr_result(FilterXExpr *expr, FilterXObject *result)
{
  gboolean success = expr->ignore_falsy_result || filterx_object_truthy(result);

  if (((!success && debug_flag) || trace_flag) && !expr->suppress_from_trace)
    {
      ScratchBuffersMarker mark;
      GString *buf = scratch_buffers_alloc_and_mark(&mark);

      if (!filterx_object_repr(result, buf))
        {
          LogMessageValueType t;
          if (!filterx_object_marshal(result, buf, &t))
            g_assert_not_reached();
        }

      if (!success)
        msg_debug("FILTERX FALSY",
                  filterx_expr_format_location_tag(expr),
                  evt_tag_mem("value", buf->str, buf->len),
                  evt_tag_str("type", filterx_object_get_type_name(result)));
      else
        msg_trace("FILTERX ESTEP",
                  filterx_expr_format_location_tag(expr),
                  evt_tag_mem("value", buf->str, buf->len),
                  evt_tag_int("truthy", filterx_object_truthy(result)),
                  evt_tag_str("type", filterx_object_get_type_name(result)));
      scratch_buffers_reclaim_marked(mark);
    }

  if (!success)
    {
      filterx_eval_push_falsy_error("bailing out due to a falsy expr", expr, result);
      return FALSE;
    }

  return TRUE;
}

static gboolean
_eval_expr(FilterXExpr *expr, FilterXObject **result)
{
  /* NOTE: this is feature envy and should be implemented within
   * filterx_expr_eval(), however that function does not depend on the
   * FilterXEvalContext layer and introducing that dependency would make the
   * whole dependency chain circular.
   */

  if (filterx_expr_has_effect(expr, FXE_WRITE))
    filterx_eval_context_make_writable(NULL);

  *result = filterx_expr_eval(expr);

  if (!(*result))
    return FALSE;

  return _process_expr_result(expr, *result);
}

static inline gboolean
_is_control_modifier_set(FilterXEvalContext *context)
{
  if (G_UNLIKELY(context->eval_control_modifier != FXC_UNSET))
    {
      /* code flow modifier detected, short circuiting */
      if (context->eval_control_modifier == FXC_BREAK)
        context->eval_control_modifier = FXC_UNSET;

      return TRUE;
    }

  return FALSE;
}

/* return value indicates if the list of expessions ran through.  *result
 * contains the value of the last expression (even if we bailed out) */
static gboolean
_eval_exprs(FilterXCompoundExpr *self, FilterXObject **result, gsize start_index)
{
  FilterXEvalContext *context = filterx_eval_get_context();

  *result = NULL;
  gsize exprs_len = filterx_expr_list_get_length(&self->exprs);
  for (gsize i = start_index; i < exprs_len; i++)
    {
      filterx_object_unref(*result);

      FilterXExpr *expr = filterx_expr_list_index_fast(&self->exprs, i);
      if (!_eval_expr(expr, result))
        return FALSE;

      if (_is_control_modifier_set(context))
        {
          filterx_object_unref(*result);
          *result = NULL;
          break;
        }
    }

  /* we exit the loop with a ref to *result, which we return */
  return TRUE;
}

static inline FilterXObject *
_process_compound_result(FilterXCompoundExpr *self, gboolean success, FilterXObject *result)
{
  if (!success)
    {
      filterx_object_unref(result);
      return NULL;
    }

  if (!result || !self->return_value_of_last_expr)
    {
      filterx_object_unref(result);

      /* an empty list of statements, or a compound statement where the
        * result is ignored.  implicitly TRUE */
      result = filterx_boolean_new(TRUE);
    }

  return result;
}

static FilterXObject *
_eval_compound_start(FilterXCompoundExpr *self, gsize start_index)
{
  FilterXObject *result = NULL;

  gboolean success = _eval_exprs(self, &result, start_index);
  result = _process_compound_result(self, success, result);
  return result;
}

static FilterXObject *
_eval_compound(FilterXExpr *s)
{
  FilterXCompoundExpr *self = (FilterXCompoundExpr *) s;

  return _eval_compound_start(self, 0);
}

FilterXObject *
filterx_compound_expr_eval_ext(FilterXExpr *s, gsize start_index)
{
  FilterXCompoundExpr *self = (FilterXCompoundExpr *) s;

  return _eval_compound_start(self, start_index);
}

static gboolean
_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXCompoundExpr *self = (FilterXCompoundExpr *) s;

  filterx_expr_list_seal(&self->exprs);
  return filterx_expr_init_method(s, cfg);
}

static void
_free(FilterXExpr *s)
{
  FilterXCompoundExpr *self = (FilterXCompoundExpr *) s;

  filterx_expr_list_clear(&self->exprs);
  filterx_expr_free_method(s);
}

/* Takes reference of expr */

gsize
filterx_compound_expr_get_count(FilterXExpr *s)
{
  FilterXCompoundExpr *self = (FilterXCompoundExpr *) s;
  return filterx_expr_list_get_length(&self->exprs);
}

void
filterx_compound_expr_add_ref(FilterXExpr *s, FilterXExpr *expr)
{
  FilterXCompoundExpr *self = (FilterXCompoundExpr *) s;

  filterx_expr_list_add_ref(&self->exprs, expr);
}

/* Takes reference of expr_list */
void
filterx_compound_expr_add_list_ref(FilterXExpr *s, GList *expr_list)
{
  for (GList *elem = expr_list; elem; elem = elem->next)
    {
      filterx_compound_expr_add_ref(s, elem->data);
    }
  g_list_free(expr_list);
}

static gboolean
_expr_walk_cb(FilterXExpr **value, gpointer user_data)
{
  gpointer *args = user_data;
  FilterXExpr *expr = args[0];
  FilterXExprWalkFunc f = args[1];
  gpointer udata = args[2];

  return filterx_expr_visit(expr, value, *f, udata);
}

gboolean
_compound_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  FilterXCompoundExpr *self = (FilterXCompoundExpr *) s;

  gpointer args[] =  { s, f, user_data };
  return filterx_expr_list_foreach_ref(&self->exprs, _expr_walk_cb, args);
}

#if SYSLOG_NG_ENABLE_JIT

#include "filterx/jit/jit.h"
#include "filterx/jit/ffi.h"

typedef enum
{
  FXC_STEP_CONTINUE = 0,
  FXC_STEP_STOP_FALSE = 1,
  FXC_STEP_STOP_TRUE = 2,
} FXCompoundExprStepAction;

__attribute__((used))
gint32
fx_jit_process_expr_result(FilterXObject *current_result, FilterXExpr *child,
                           FilterXEvalContext *context, FilterXObject **last_result)
{
  /* unref() previous child */
  filterx_object_unref(*last_result);

  if (!current_result)
    {
      *last_result = NULL;
      return FXC_STEP_STOP_FALSE;
    }

  if (!_process_expr_result(child, current_result))
    {
      filterx_object_unref(current_result);
      *last_result = NULL;
      return FXC_STEP_STOP_FALSE;
    }

  if (_is_control_modifier_set(context))
    {
      filterx_object_unref(current_result);
      *last_result = NULL;
      return FXC_STEP_STOP_TRUE;
    }

  *last_result = current_result;
  return FXC_STEP_CONTINUE;
}

__attribute__((used))
FilterXObject *
fx_jit_process_compound_result(FilterXCompoundExpr *self, gint32 success, FilterXObject *result)
{
  return _process_compound_result(self, success, result);
}

static inline FilterXIRValue
_emit_eval_get_context(FilterXJIT *jit)
{
  FilterXJITFFI *ffi = filterx_jit_get_ffi(jit);
  return fx_jit_emit_extern_call(jit, "filterx_eval_get_context", ffi->ptr_ty, NULL, NULL, 0);
}

static inline FilterXIRValue
_emit_process_compound_result(FilterXJIT *jit, FilterXCompoundExpr *self, FilterXIRValue success, FilterXIRValue result)
{
  FilterXJITFFI *ffi = filterx_jit_get_ffi(jit);
  return fx_jit_emit_extern_call(jit, "fx_jit_process_compound_result", ffi->ptr_ty,
                                 (LLVMTypeRef[]){ ffi->ptr_ty, ffi->i32_ty, ffi->ptr_ty },
                                 (FilterXIRValue[]){ fx_jit_emit_const_ptr(jit, self), success, result }, 3);
}

static inline FilterXIRValue
_emit_process_expr_result(FilterXJIT *jit, FilterXIRValue result, FilterXExpr *child, FilterXIRValue eval_ctx, FilterXIRValue result_slot)
{
  FilterXJITFFI *ffi = filterx_jit_get_ffi(jit);
  return fx_jit_emit_extern_call(jit, "fx_jit_process_expr_result", ffi->i32_ty,
                                (LLVMTypeRef[]){ ffi->ptr_ty, ffi->ptr_ty, ffi->ptr_ty, ffi->ptr_ty },
                                (FilterXIRValue[]){ result, fx_jit_emit_const_ptr(jit, child),
                                                    eval_ctx, result_slot }, 4);
}

static FilterXIRValue
_compound_compile(FilterXExpr *s, FilterXJIT *jit)
{
  FilterXCompoundExpr *self = (FilterXCompoundExpr *) s;
  FilterXJITFFI *ffi = filterx_jit_get_ffi(jit);
  FilterXIRBuilder ir = filterx_jit_get_ir_builder(jit);
  FilterXIRValue block = filterx_jit_ir_get_current_block(jit);

  FilterXIRValue eval_ctx = _emit_eval_get_context(jit);
  FilterXIRValue result_slot = LLVMBuildAlloca(ir, ffi->ptr_ty, "result");
  FilterXIRValue success_slot = LLVMBuildAlloca(ir, ffi->i32_ty, "success");

  LLVMBuildStore(ir, LLVMConstNull(ffi->ptr_ty), result_slot);
  LLVMBuildStore(ir, LLVMConstInt(ffi->i32_ty, TRUE, FALSE), success_slot);

  FilterXIRSequence mark_failure = filterx_jit_ir_create_sequence(jit, "mark_failure", block);
  FilterXIRSequence finish = filterx_jit_ir_create_sequence(jit, "finish", block);

  gsize n = filterx_expr_list_get_length(&self->exprs);
  if (n == 0)
    {
      LLVMBuildBr(ir, finish);
      goto exit;
    }

  FilterXIRValue prev_switch = NULL;
  for (gsize i = 0; i < n; i++)
    {
      FilterXExpr *child = filterx_expr_list_index_fast(&self->exprs, i);
      FilterXIRSequence stmt_seq = filterx_jit_ir_add_new_sequence_to_block(jit, filterx_expr_get_text(child), block);

      if (prev_switch)
        LLVMAddCase(prev_switch, LLVMConstInt(ffi->i32_ty, FXC_STEP_CONTINUE, FALSE), stmt_seq);
      else
        LLVMBuildBr(ir, stmt_seq);

      filterx_jit_ir_set_insert_point_to_sequence_tail(jit, stmt_seq);

      if (child->lloc)
        filterx_jit_ir_set_source_location(jit, child->lloc->name ? : "<filterx>",
                                           child->lloc->first_line, child->lloc->first_column);

      if (filterx_expr_has_effect(child, FXE_WRITE))
        fx_jit_emit_eval_context_make_writable(jit, eval_ctx);

      FilterXIRValue result = filterx_expr_compile_or_eval(child, jit);
      FilterXIRValue action = _emit_process_expr_result(jit, result, child, eval_ctx, result_slot);

      prev_switch = LLVMBuildSwitch(ir, action, mark_failure, 2);
      LLVMAddCase(prev_switch, LLVMConstInt(ffi->i32_ty, FXC_STEP_STOP_TRUE, FALSE), finish);
    }
  LLVMAddCase(prev_switch, LLVMConstInt(ffi->i32_ty, FXC_STEP_CONTINUE, FALSE), finish);

exit:
  /* mark failure */
  filterx_jit_ir_add_sequence_to_block(jit, mark_failure, block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(jit, mark_failure);
  LLVMBuildStore(ir, LLVMConstInt(ffi->i32_ty, FALSE, FALSE), success_slot);
  LLVMBuildBr(ir, finish);

  /* finish */
  filterx_jit_ir_add_sequence_to_block(jit, finish, block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(jit, finish);
  FilterXIRValue result_val = LLVMBuildLoad2(ir, ffi->ptr_ty, result_slot, "result");
  FilterXIRValue success_val = LLVMBuildLoad2(ir, ffi->i32_ty, success_slot, "success");
  return _emit_process_compound_result(jit, self, success_val, result_val);
}

#endif

FilterXExpr *
filterx_compound_expr_new(gboolean return_value_of_last_expr)
{
  FilterXCompoundExpr *self = g_new0(FilterXCompoundExpr, 1);

  filterx_expr_init_instance(&self->super, "compound", FXE_READ);
  self->super.eval = _eval_compound;
  self->super.init = _init;
  self->super.walk_children = _compound_walk;
  self->super.free_fn = _free;
#if SYSLOG_NG_ENABLE_JIT
  self->super.compile = _compound_compile;
#endif
  self->return_value_of_last_expr = return_value_of_last_expr;
  filterx_expr_list_init(&self->exprs);

  return &self->super;
}

FilterXExpr *
filterx_compound_expr_new_va(gboolean return_value_of_last_expr, FilterXExpr *first, ...)
{
  FilterXExpr *s = filterx_compound_expr_new(return_value_of_last_expr);
  va_list va;

  va_start(va, first);
  FilterXExpr *arg = first;
  while (arg)
    {
      filterx_compound_expr_add_ref(s, arg);
      arg = va_arg(va, FilterXExpr *);
    }
  va_end(va);
  return s;
}
