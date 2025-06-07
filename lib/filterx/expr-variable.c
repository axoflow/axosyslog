/*
 * Copyright (c) 2023 Balazs Scheidler <balazs.scheidler@axoflow.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
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
} FilterXVariableExpr;

static FilterXObject *
_eval_macro(FilterXVariableExpr *self, FilterXEvalContext *context)
{
  gssize value_len;
  LogMessageValueType t = LM_VT_NONE;

  const gchar *value = log_msg_get_value_if_set_with_type(context->msg, self->handle, &value_len, &t);
  if (!value)
    {
      filterx_eval_context_push_error(context, "Variable is unset", &self->super, self->variable_name);
      return NULL;
    }
  return filterx_unmarshal_repr(value, value_len, t);
}

static FilterXObject *
_eval_variable(FilterXExpr *s)
{
  FilterXVariableExpr *self = (FilterXVariableExpr *) s;
  FilterXEvalContext *context = filterx_eval_get_context();
  FilterXVariable *variable;

  if (self->handle_is_macro)
    return _eval_macro(self, context);

  variable = filterx_scope_lookup_variable(context->scope, self->handle);
  if (variable)
    {
      FilterXObject *value = filterx_scope_get_variable(context->scope, variable);
      if (!value)
        {
          filterx_eval_context_push_error(context, "Variable is unset", &self->super, self->variable_name);
        }
      return value;
    }

  if (self->variable_type == FX_VAR_MESSAGE_TIED)
    {
      /* auto register message tied variables */
      variable = filterx_scope_register_variable(context->scope, self->variable_type, self->handle);
      if (variable)
        {
          FilterXObject *value = filterx_scope_get_variable(context->scope, variable);
          if (!value)
            {
              filterx_eval_context_push_error(context, "Variable is unset", &self->super, self->variable_name);
            }
          return value;
        }
    }

  filterx_eval_context_push_error(context, "No such variable", s, self->variable_name);
  return NULL;
}

static void
_update_repr(FilterXExpr *s, FilterXObject **new_repr)
{
  FilterXVariableExpr *self = (FilterXVariableExpr *) s;
  FilterXEvalContext *context = filterx_eval_get_context();
  FilterXScope *scope = context->scope;

  FilterXVariable *variable = filterx_scope_lookup_variable(scope, self->handle);
  filterx_scope_set_variable(scope, variable, new_repr, FALSE);
}

static gboolean
_assign(FilterXExpr *s, FilterXObject **new_value)
{
  FilterXVariableExpr *self = (FilterXVariableExpr *) s;
  FilterXEvalContext *context = filterx_eval_get_context();

  if (self->handle_is_macro)
    {
      filterx_eval_context_push_error(context, "Macro based variable cannot be changed", &self->super, self->variable_name);
      return FALSE;
    }

  FilterXScope *scope = context->scope;
  FilterXVariable *variable = filterx_scope_lookup_variable(scope, self->handle);

  if (!variable)
    variable = filterx_scope_register_variable(scope, self->variable_type, self->handle);

  filterx_scope_set_variable(scope, variable, new_value, TRUE);
  return TRUE;
}

static gboolean
_isset(FilterXExpr *s)
{
  FilterXVariableExpr *self = (FilterXVariableExpr *) s;
  FilterXEvalContext *context = filterx_eval_get_context();
  FilterXScope *scope = context->scope;
  LogMessage *msg = context->msg;

  FilterXVariable *variable = filterx_scope_lookup_variable(scope, self->handle);
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

  FilterXEvalContext *context = filterx_eval_get_context();
  if (self->handle_is_macro)
    {
      filterx_eval_context_push_error(context, "Macro based variable cannot be changed", &self->super, self->variable_name);
      return FALSE;
    }

  FilterXScope *scope = context->scope;
  LogMessage *msg = context->msg;

  FilterXVariable *variable = filterx_scope_lookup_variable(context->scope, self->handle);
  if (variable)
    {
      filterx_scope_unset_variable(scope, variable);
      return TRUE;
    }

  if (self->variable_type == FX_VAR_MESSAGE_TIED)
    {
      if (log_msg_is_value_set(msg, self->handle))
        {
          FilterXVariable *v = filterx_scope_register_variable(context->scope, self->variable_type, self->handle);
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
  FilterXObject *dollar_name_obj = filterx_string_new(dollar_name, -1);
  g_free(dollar_name);

  return dollar_name_obj;
}

static FilterXExpr *
filterx_variable_expr_new(const gchar *name, FilterXVariableType variable_type)
{
  FilterXVariableExpr *self = g_new0(FilterXVariableExpr, 1);

  filterx_expr_init_instance(&self->super, FILTERX_EXPR_TYPE_NAME(variable));
  self->super.free_fn = _free;
  self->super.eval = _eval_variable;
  self->super._update_repr = _update_repr;
  self->super.assign = _assign;
  self->super.is_set = _isset;
  self->super.unset = _unset;

  self->variable_type = variable_type;

  self->handle = filterx_map_varname_to_handle(name, variable_type);
  if (variable_type == FX_VAR_MESSAGE_TIED)
    {
      self->variable_name = _dollar_msg_varname(name);
      self->handle_is_macro = log_msg_is_handle_macro(filterx_variable_handle_to_nv_handle(self->handle));
    }
  else
    self->variable_name = filterx_string_new(name, -1);

  /* NOTE: name borrows the string value from the string object */
  self->super.name = filterx_string_get_value_ref(self->variable_name, NULL);

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
