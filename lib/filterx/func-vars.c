/*
 * Copyright (c) 2024 Attila Szakacs
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

#include "func-vars.h"
#include "filterx-eval.h"
#include "object-json.h"
#include "object-string.h"

static gboolean
_add_to_dict(FilterXVariable *variable, gpointer user_data)
{
  FilterXObject *vars = (FilterXObject *) user_data;

  gssize name_len;
  const gchar *name_str = filterx_variable_get_name(variable, &name_len);
  FilterXObject *name = filterx_string_new(name_str, name_len);

  FilterXObject *value = filterx_variable_get_value(variable);
  FilterXObject *cloned_value = filterx_object_clone(value);
  filterx_object_unref(value);

  gboolean success = filterx_object_set_subscript(vars, name, &cloned_value);

  filterx_object_unref(cloned_value);
  filterx_object_unref(name);
  return success;
}

FilterXObject *
filterx_simple_function_vars(FilterXExpr *s, GPtrArray *args)
{
  if (args && args->len != 0)
    {
      filterx_simple_function_argument_error(s, "Incorrect number of arguments", FALSE);
      return NULL;
    }

  FilterXEvalContext *context = filterx_eval_get_context();
  FilterXObject *vars = filterx_json_object_new_empty();

  if (filterx_scope_foreach_variable(context->scope, _add_to_dict, vars))
    return vars;

  filterx_object_unref(vars);
  return NULL;
}
