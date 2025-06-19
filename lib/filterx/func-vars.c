/*
 * Copyright (c) 2024 Attila Szakacs
 * Copyright (c) 2025 László Várady
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

#include "func-vars.h"
#include "filterx-eval.h"
#include "object-dict.h"
#include "object-string.h"
#include "object-primitive.h"
#include "object-message-value.h"
#include "object-dict-interface.h"

#include "scratch-buffers.h"
#include "str-utils.h"

typedef struct _FilterXFunctionVars
{
  FilterXFunction super;
  gboolean exclude_msg_values;
} FilterXFunctionVars;

static gboolean
_add_to_dict(FilterXVariable *variable, gpointer user_data)
{
  FilterXFunctionVars *self = ((gpointer *)(user_data))[0];
  FilterXObject *vars = ((gpointer *)(user_data))[1];
  GString *name_buf = ((gpointer *)(user_data))[2];

  if (filterx_variable_is_message_tied(variable) && self->exclude_msg_values)
    return TRUE;

  gssize name_len;
  const gchar *name_str = filterx_variable_get_name(variable, &name_len);

  if (filterx_variable_is_message_tied(variable))
    {
      g_string_assign(name_buf, "$");
      g_string_append_len(name_buf, name_str, name_len);
      name_str = name_buf->str;
      name_len = name_buf->len;
    }

  FILTERX_STRING_DECLARE_ON_STACK(name, name_str, name_len);

  FilterXObject *value = filterx_variable_get_value(variable);
  FilterXObject *cloned_value = filterx_object_clone(value);
  filterx_object_unref(value);

  gboolean success = filterx_object_set_subscript(vars, name, &cloned_value);

  filterx_object_unref(cloned_value);
  filterx_object_unref(name);
  return success;
}

static FilterXObject *
_filterx_function_vars_eval(FilterXExpr *s)
{
  FilterXFunctionVars *self = (FilterXFunctionVars *) s;
  FilterXEvalContext *context = filterx_eval_get_context();
  FilterXObject *vars = filterx_dict_new();

  ScratchBuffersMarker marker;
  GString *name_buf = scratch_buffers_alloc_and_mark(&marker);

  gpointer user_data[] = { self, vars, name_buf };
  if (!filterx_scope_foreach_variable_readonly(context->scope, _add_to_dict, user_data))
    {
      filterx_object_unref(vars);
      vars = NULL;
    }

  scratch_buffers_reclaim_marked(marker);
  return vars;
}

FilterXExpr *
filterx_function_vars_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionVars *self = g_new0(FilterXFunctionVars, 1);
  filterx_function_init_instance(&self->super, "vars");

  self->super.super.eval = _filterx_function_vars_eval;

  gboolean exists, eval_error;
  self->exclude_msg_values = filterx_function_args_get_named_literal_boolean(args, "exclude_msg_values", &exists,
                             &eval_error);

  if (!filterx_function_args_check(args, error))
    goto error;

  if (filterx_function_args_len(args) != 0)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "function has only optional parameters");
      goto error;
    }

  if (eval_error)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "exclude_msg_values argument must be boolean literal");
      goto error;
    }

  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}


static gboolean
_load_from_dict(FilterXObject *key, FilterXObject *value, gpointer user_data)
{
  FilterXExpr *s = ((gpointer *)(user_data))[0];
  FilterXScope *scope = ((gpointer *)(user_data))[1];

  if (!filterx_object_is_type(key, &FILTERX_TYPE_NAME(string)))
    {
      filterx_eval_push_error("Variable name must be a string", s, key);
      return FALSE;
    }

  gsize key_len = 0;
  const gchar *key_str = filterx_string_get_value_ref(key, &key_len);
  APPEND_ZERO(key_str, key_str, key_len);

  if (key_len == 0)
    {
      filterx_eval_push_error("Variable name must not be empty", s, key);
      return FALSE;
    }

  FilterXVariableType variable_type = FX_VAR_DECLARED_FLOATING;
  FilterXVariableHandle handle;
  if (key_str[0] == '$')
    {
      variable_type = FX_VAR_MESSAGE_TIED;
      handle = filterx_map_varname_to_handle(key_str + 1, variable_type);
    }
  else
    {
      handle = filterx_map_varname_to_handle(key_str, variable_type);
    }

  FilterXVariable *variable = filterx_scope_register_variable(scope, variable_type, handle);
  if (!variable)
    {
      filterx_eval_push_error("Failed to register variable", NULL, key);
      return FALSE;
    }

  FilterXObject *cloned_value = filterx_object_clone(value);
  filterx_scope_set_variable(scope, variable, &cloned_value, TRUE);
  filterx_object_unref(cloned_value);

  if (debug_flag)
    {
      LogMessageValueType type;

      GString *repr = scratch_buffers_alloc();
      if (!filterx_object_repr(value, repr))
        filterx_object_marshal_append(value, repr, &type);

      msg_trace("FILTERX LOADV",
                filterx_expr_format_location_tag(s),
                evt_tag_str("key", key_str),
                evt_tag_str("value", repr->str),
                evt_tag_str("type", value->type->name),
                evt_tag_int("variable_type", variable_type));
    }

  return TRUE;
}

FilterXObject *
filterx_simple_function_load_vars(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  if (!args || args_len != 1)
    {
      filterx_simple_function_argument_error(s, "Incorrect number of arguments", FALSE);
      return NULL;
    }

  FilterXObject *vars = args[0];
  FilterXObject *vars_unwrapped = filterx_ref_unwrap_ro(vars);

  FilterXScope *scope = filterx_eval_get_scope();
  gpointer user_data[] = { s, scope };
  gboolean success = filterx_dict_iter(vars_unwrapped, _load_from_dict, user_data);

  return success ? filterx_boolean_new(TRUE) : NULL;
}
