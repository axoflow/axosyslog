/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 László Várady
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

#include "filterx-variable.h"

FilterXVariableHandle
filterx_map_varname_to_handle(const gchar *name, FilterXVariableType type)
{
  NVHandle nv_handle = log_msg_get_value_handle(name);

  if (type == FX_VAR_MESSAGE_TIED)
    return (FilterXVariableHandle) nv_handle;
  return (FilterXVariableHandle) nv_handle | FILTERX_HANDLE_FLOATING_BIT;
}

void
filterx_variable_clear(FilterXVariable *v)
{
  filterx_object_unref(v->value);
}

void
filterx_variable_init_instance(FilterXVariable *v,
                               FilterXVariableType variable_type,
                               FilterXVariableHandle handle)
{
  v->variable_type = variable_type;
  v->handle = handle;
}
