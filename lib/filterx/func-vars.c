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
#include "filterx-mapping.h"

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
  FilterXObject *cloned_value = filterx_object_copy(value);
  filterx_object_unref(value);

  gboolean success = filterx_object_set_subscript(vars, name, &cloned_value);

  filterx_object_unref(cloned_value);
  FILTERX_STRING_CLEAR_FROM_STACK(name);
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

static gboolean
_vars_walk(FilterXExpr *s, FilterXExprWalkFunc f, gpointer user_data)
{
  /* no child expressions */
  return TRUE;
}

FilterXExpr *
filterx_function_vars_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionVars *self = g_new0(FilterXFunctionVars, 1);

  filterx_function_init_instance(&self->super, "vars", FXE_READ);

  self->super.super.eval = _filterx_function_vars_eval;
  self->super.super.walk_children = _vars_walk;

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

FilterXExpr *
filterx_function_load_vars_new(FilterXFunctionArgs *args, GError **error)
{
  g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
              "function has been deprecated and removed");
  filterx_function_args_free(args);
  return NULL;
}
