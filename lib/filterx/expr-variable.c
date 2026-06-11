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
#include "filterx/expr-variable.h"
#include "filterx/object-message-value.h"
#include "filterx/object-string.h"
#include "filterx/filterx-scope.h"
#include "filterx/filterx-eval.h"
#include "filterx/filterx-variable.h"
#include "logmsg/logmsg.h"
#include "stats/stats-registry.h"
#include "stats/stats-cluster-single.h"


typedef struct _FilterXVariableExpr
{
  FilterXExpr super;
  FilterXVariableType variable_type;
  NVHandle handle;
  FilterXObject *variable_name;
  guint32 handle_is_macro:1;
  gint scope_var_idx;
} FilterXVariableExpr;

static FilterXObject *
_eval_macro(FilterXVariableExpr *self, FilterXEvalContext *context)
{
  gssize value_len;
  LogMessageValueType t = LM_VT_NONE;

  const gchar *value = log_msg_get_value_if_set_with_type(context->msg, self->handle, &value_len, &t);
  if (!value)
    {
      filterx_eval_push_error("Variable is unset", &self->super, self->variable_name);
      return NULL;
    }
  return filterx_unmarshal_repr(value, value_len, t);
}

static FilterXObject *
_borrow_variable_value(FilterXVariableExpr *self, FilterXEvalContext *context)
{
  FilterXVariable *variable = filterx_scope_lookup_variable(context->scope, self->handle, self->scope_var_idx);

  if (!variable && self->variable_type == FX_VAR_MESSAGE_TIED)
    {
      /* auto register message tied variables */
      variable = filterx_scope_register_variable(context->scope, self->variable_type, self->handle,
                                                 self->scope_var_idx);
    }

  if (!variable)
    {
      filterx_eval_push_error("No such variable", &self->super, self->variable_name);
      return NULL;
    }

  FilterXObject *value = filterx_variable_borrow_value(variable);
  if (!value)
    filterx_eval_push_error("Variable is unset", &self->super, self->variable_name);

  return value;
}

static FilterXObject *
_eval_variable_in_scope(FilterXVariableExpr *self, FilterXEvalContext *context)
{
  return filterx_object_ref(_borrow_variable_value(self, context));
}

static FilterXObject *
_eval_variable(FilterXExpr *s)
{
  FilterXVariableExpr *self = (FilterXVariableExpr *) s;
  FilterXEvalContext *context = filterx_eval_get_context();

  if (self->handle_is_macro)
    return _eval_macro(self, context);

  return _eval_variable_in_scope(self, context);
}

static void
_update_repr(FilterXExpr *s, FilterXObject **new_repr)
{
  FilterXVariableExpr *self = (FilterXVariableExpr *) s;
  FilterXEvalContext *context = filterx_eval_get_context();
  FilterXScope *scope = context->scope;

  FilterXVariable *variable = filterx_scope_lookup_variable(scope, self->handle, self->scope_var_idx);
  filterx_scope_set_variable(scope, variable, new_repr, variable->assigned);
}

static void
_assign_variable_in_scope(FilterXVariableExpr *self, FilterXEvalContext *context, FilterXObject **new_value)
{
  FilterXScope *scope = context->scope;
  FilterXVariable *variable = filterx_scope_lookup_variable(scope, self->handle, self->scope_var_idx);

  if (!variable)
    variable = filterx_scope_register_variable(scope, self->variable_type, self->handle, self->scope_var_idx);

  filterx_scope_set_variable(scope, variable, new_value, TRUE);
}

static gboolean
_assign(FilterXExpr *s, FilterXObject **new_value)
{
  FilterXVariableExpr *self = (FilterXVariableExpr *) s;

  if (self->handle_is_macro)
    {
      filterx_eval_push_error("Macro based variable cannot be changed", &self->super, self->variable_name);
      return FALSE;
    }

  _assign_variable_in_scope(self, filterx_eval_get_context(), new_value);
  return TRUE;
}

static gboolean
_isset(FilterXExpr *s)
{
  FilterXVariableExpr *self = (FilterXVariableExpr *) s;
  FilterXEvalContext *context = filterx_eval_get_context();
  FilterXScope *scope = context->scope;
  LogMessage *msg = context->msg;

  FilterXVariable *variable = filterx_scope_lookup_variable(scope, self->handle, self->scope_var_idx);
  if (variable)
    return filterx_variable_is_set(variable);

  if (self->variable_type == FX_VAR_MESSAGE_TIED)
    return log_msg_is_value_set(msg, filterx_variable_handle_to_nv_handle(self->handle));
  return FALSE;
}

static gboolean
_unset(FilterXExpr *s)
{
  FilterXVariableExpr *self = (FilterXVariableExpr *) s;

  if (self->handle_is_macro)
    {
      filterx_eval_push_error("Macro based variable cannot be changed", &self->super, self->variable_name);
      return FALSE;
    }

  FilterXEvalContext *context = filterx_eval_get_context();
  FilterXScope *scope = context->scope;
  LogMessage *msg = context->msg;

  FilterXVariable *variable = filterx_scope_lookup_variable(context->scope, self->handle, self->scope_var_idx);
  if (variable)
    {
      filterx_scope_unset_variable(scope, variable);
      return TRUE;
    }

  if (self->variable_type == FX_VAR_MESSAGE_TIED)
    {
      if (log_msg_is_value_set(msg, self->handle))
        {
          FilterXVariable *v = filterx_scope_register_variable(context->scope, self->variable_type, self->handle,
                                                               self->scope_var_idx);
          /* make sure it is considered changed */
          filterx_scope_unset_variable(context->scope, v);
        }
    }

  return TRUE;
}

static void
_free(FilterXExpr *s)
{
  FilterXVariableExpr *self = (FilterXVariableExpr *) s;

  filterx_object_unref(self->variable_name);
  filterx_expr_free_method(s);
}

static inline FilterXObject *
_dollar_msg_varname(const gchar *name)
{
  gchar *dollar_name = g_strdup_printf("$%s", name);
  FilterXObject *dollar_name_obj = filterx_string_new_frozen(dollar_name);
  g_free(dollar_name);

  return dollar_name_obj;
}

static gboolean
_variable_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  return TRUE;
}

#if SYSLOG_NG_ENABLE_JIT

#include "filterx/jit/jit.h"
#include "filterx/jit/ffi.h"

__attribute__((used))
FilterXObject *
fx_jit_eval_variable(FilterXExpr *s)
{
  return _eval_variable(s);
}

__attribute__((used))
FilterXObject *
fx_jit_borrow_variable_value(FilterXEvalContext *context, FilterXExpr *s)
{
  return _borrow_variable_value((FilterXVariableExpr *) s, context);
}

__attribute__((used))
FilterXObject *
fx_jit_assign_variable_in_scope(FilterXEvalContext *context, FilterXExpr *s, FilterXObject *new_value)
{
  _assign_variable_in_scope((FilterXVariableExpr *) s, context, &new_value);
  return new_value;
}

static FilterXIRValue
_variable_compile(FilterXExpr *s, FilterXJIT *jit)
{
  FilterXVariableExpr *self = (FilterXVariableExpr *) s;
  FilterXJITFFI *ffi = filterx_jit_get_ffi(jit);

  if (self->handle_is_macro || self->scope_var_idx < 0)
    {
      FilterXIRValue args[] = { fx_jit_emit_const_ptr(jit, self) };
      FilterXIRType param_tys[] = { ffi->ptr_ty };
      return fx_jit_emit_extern_call(jit, "fx_jit_eval_variable", ffi->ptr_ty, param_tys, args, 1);
    }

  FilterXIRBuilder ir = filterx_jit_get_ir_builder(jit);
  FilterXIRValue block = filterx_jit_ir_get_current_block(jit);
  FilterXIRValue local = filterx_jit_ir_get_variable(jit, self->scope_var_idx);

  FilterXIRSequence cached_seq = LLVMGetInsertBlock(ir);
  FilterXIRValue cached = LLVMBuildLoad2(ir, ffi->ptr_ty, local, "var_value");

  FilterXIRSequence fill = filterx_jit_ir_create_sequence(jit, "var_fill", block);
  FilterXIRSequence merge = filterx_jit_ir_create_sequence(jit, "var_loaded", block);

  /* NULL is a loaded-but-unset value; only the sentinel means "not loaded yet" */
  FilterXIRValue is_unloaded = filterx_jit_ir_is_variable_uninitialized(jit, cached);
  LLVMBuildCondBr(ir, is_unloaded, fill, merge);

  filterx_jit_ir_add_sequence_to_block(jit, fill, block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(jit, fill);
  FilterXIRValue args[] = { filterx_jit_ir_get_eval_context(jit), fx_jit_emit_const_ptr(jit, self) };
  FilterXIRType param_tys[] = { ffi->ptr_ty, ffi->ptr_ty };
  FilterXIRValue filled = fx_jit_emit_extern_call(jit, "fx_jit_borrow_variable_value",
                                                  ffi->ptr_ty, param_tys, args, 2);
  LLVMBuildStore(ir, filled, local);
  LLVMBuildBr(ir, merge);

  filterx_jit_ir_add_sequence_to_block(jit, merge, block);
  filterx_jit_ir_set_insert_point_to_sequence_tail(jit, merge);
  FilterXIRValue value = LLVMBuildPhi(ir, ffi->ptr_ty, "var_value");
  FilterXIRValue incoming_values[] = { cached, filled };
  FilterXIRSequence incoming_blocks[] = { cached_seq, fill };
  LLVMAddIncoming(value, incoming_values, incoming_blocks, 2);

  return fx_jit_emit_object_ref(jit, value);
}

static FilterXIRValue
_variable_compile_assign(FilterXExpr *s, FilterXJIT *jit, FilterXIRValue new_value)
{
  FilterXVariableExpr *self = (FilterXVariableExpr *) s;
  FilterXJITFFI *ffi = filterx_jit_get_ffi(jit);
  FilterXIRBuilder ir = filterx_jit_get_ir_builder(jit);
  FilterXIRValue local = filterx_jit_ir_get_variable(jit, self->scope_var_idx);

  FilterXIRValue args[] =
  {
    filterx_jit_ir_get_eval_context(jit),
    fx_jit_emit_const_ptr(jit, self),
    new_value,
  };
  FilterXIRType param_tys[] = { ffi->ptr_ty, ffi->ptr_ty, ffi->ptr_ty };
  FilterXIRValue stored = fx_jit_emit_extern_call(jit, "fx_jit_assign_variable_in_scope",
                                                  ffi->ptr_ty, param_tys, args, 3);

  LLVMBuildStore(ir, stored, local);
  return stored;
}

void
filterx_variable_expr_compile_repr_update(FilterXExpr *s, FilterXJIT *jit, FilterXIRValue new_repr)
{
  FilterXVariableExpr *self = (FilterXVariableExpr *) s;

  if (self->handle_is_macro || self->scope_var_idx < 0)
    return;

  FilterXIRBuilder ir = filterx_jit_get_ir_builder(jit);
  FilterXIRValue local = filterx_jit_ir_get_variable(jit, self->scope_var_idx);
  LLVMBuildStore(ir, new_repr, local);
}

#endif

static FilterXExpr *
filterx_variable_expr_new(const gchar *name, FilterXVariableType variable_type)
{
  FilterXVariableExpr *self = g_new0(FilterXVariableExpr, 1);

  filterx_expr_init_instance(&self->super, FILTERX_EXPR_TYPE_NAME(variable), FXE_READ);
  self->super.walk_children = _variable_walk;
  self->super.free_fn = _free;
  self->super.eval = _eval_variable;
#if SYSLOG_NG_ENABLE_JIT
  self->super.compile = _variable_compile;
#endif
  self->super._update_repr = _update_repr;
  self->super.assign = _assign;
  self->super.is_set = _isset;
  self->super.unset = _unset;

  self->variable_type = variable_type;
  self->scope_var_idx = -1;

  self->handle = filterx_map_varname_to_handle(name, variable_type);
  if (variable_type == FX_VAR_MESSAGE_TIED)
    {
      self->variable_name = _dollar_msg_varname(name);
      self->handle_is_macro = log_msg_is_handle_macro(filterx_variable_handle_to_nv_handle(self->handle));
    }
  else
    self->variable_name = filterx_string_new_frozen(name);

  /* NOTE: name borrows the string value from the string object */
  self->super.name = filterx_string_get_value_ref_and_assert_nul(self->variable_name, NULL);

  return &self->super;
}

FilterXExpr *
filterx_msg_variable_expr_new(const gchar *name)
{
  return filterx_variable_expr_new(name, FX_VAR_MESSAGE_TIED);
}

FilterXExpr *
filterx_floating_variable_expr_new(const gchar *name)
{
  return filterx_variable_expr_new(name, FX_VAR_FLOATING);
}

FilterXVariableHandle
filterx_variable_expr_get_handle(FilterXExpr *s)
{
  FilterXVariableExpr *self = (FilterXVariableExpr *) s;

  return self->handle;
}

gboolean
filterx_variable_expr_is_macro(FilterXExpr *s)
{
  FilterXVariableExpr *self = (FilterXVariableExpr *) s;

  return !!self->handle_is_macro;
}

void
filterx_variable_expr_set_scope_var_idx(FilterXExpr *s, gint idx)
{
  FilterXVariableExpr *self = (FilterXVariableExpr *) s;

  self->scope_var_idx = idx;

#if SYSLOG_NG_ENABLE_JIT
  if (!self->handle_is_macro && idx >= 0)
    self->super.compile_assign = _variable_compile_assign;
#endif
}

gint
filterx_variable_expr_get_scope_var_idx(FilterXExpr *s)
{
  FilterXVariableExpr *self = (FilterXVariableExpr *) s;

  return self->scope_var_idx;
}

void
filterx_variable_expr_declare(FilterXExpr *s)
{
  FilterXVariableExpr *self = (FilterXVariableExpr *) s;

  g_assert(filterx_expr_is_variable(s));
  /* we can only declare a floating variable */
  g_assert(self->variable_type == FX_VAR_FLOATING);
  self->variable_type = FX_VAR_DECLARED_FLOATING;
}

FILTERX_EXPR_DEFINE_TYPE(variable);
