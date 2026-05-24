/*
 * Copyright (c) 2026 László Várady
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

#ifndef FILTERX_SCOPE_VAR_LAYOUT_H
#define FILTERX_SCOPE_VAR_LAYOUT_H

#include "filterx/filterx-expr.h"
#include "filterx/filterx-variable.h"

typedef struct _FilterXScopeVariableLayout
{
  FilterXVariable *layout;
  guint32 num_variables;
} FilterXScopeVariableLayout;

FilterXScopeVariableLayout *filterx_scope_variable_layout_new(FilterXExpr *root);
void filterx_scope_variable_layout_free(FilterXScopeVariableLayout *self);

static inline void
filterx_scope_variable_layout_fill(FilterXScopeVariableLayout *self, FilterXVariable *variables)
{
  if (!self)
    return;

  memcpy(variables, self->layout, self->num_variables * sizeof(FilterXVariable));
}

#endif
