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

#include "filterx/filterx-scope-var-layout.h"
#include "filterx/expr-variable.h"

typedef struct _LayoutBuilder
{
  GHashTable *handles_seen;
  GPtrArray *exprs;
} LayoutBuilder;

static inline void
_collect_variable(FilterXExpr *expr, LayoutBuilder *b)
{
  if (filterx_variable_expr_is_macro(expr))
    return;

  FilterXVariableHandle handle;

  filterx_variable_expr_get_handle(expr, &handle);
  g_hash_table_add(b->handles_seen, GUINT_TO_POINTER((guint) handle));
  g_ptr_array_add(b->exprs, expr);
}

static gboolean
_collect_variables_in_child_exprs(FilterXExpr *parent, FilterXExpr **child, gpointer user_data)
{
  LayoutBuilder *b = (LayoutBuilder *) user_data;
  FilterXExpr *expr = *child;

  if (filterx_expr_is_variable(expr))
    _collect_variable(expr, b);

  filterx_expr_walk_children(expr, _collect_variables_in_child_exprs, b);
  return TRUE;
}

static int
_compare_handles(gconstpointer a, gconstpointer b)
{
  FilterXVariableHandle ha = *(const FilterXVariableHandle *) a;
  FilterXVariableHandle hb = *(const FilterXVariableHandle *) b;
  return (ha < hb) ? -1 : (ha > hb) ? 1 : 0;
}

static void
_build_layout(FilterXScopeVariableLayout *self, LayoutBuilder *b)
{
  self->num_variables = g_hash_table_size(b->handles_seen);

  if (self->num_variables == 0)
    return;

  self->handles = g_new(FilterXVariableHandle, self->num_variables);

  GHashTableIter iter;
  gpointer key;
  guint i = 0;
  g_hash_table_iter_init(&iter, b->handles_seen);
  while (g_hash_table_iter_next(&iter, &key, NULL))
    self->handles[i++] = (FilterXVariableHandle) GPOINTER_TO_UINT(key);

  /* scope variable lookup requires sorted array */
  qsort(self->handles, self->num_variables, sizeof(FilterXVariableHandle), _compare_handles);

  GHashTable *handle_to_idx = g_hash_table_new(g_direct_hash, g_direct_equal);

  for (gint idx = 0; idx < self->num_variables; idx++)
    {
      g_hash_table_insert(handle_to_idx, GUINT_TO_POINTER((guint) self->handles[idx]),
                          GINT_TO_POINTER((gint) idx));
    }

  FilterXVariableHandle handle;

  for (gint j = 0; j < b->exprs->len; j++)
    {
      FilterXExpr *expr = g_ptr_array_index(b->exprs, j);
      g_assert(filterx_expr_is_variable(expr));
      filterx_variable_expr_get_handle(expr, &handle);
      gpointer idx = g_hash_table_lookup(handle_to_idx, GUINT_TO_POINTER((guint) handle));

      filterx_variable_expr_set_scope_var_idx(expr, GPOINTER_TO_INT(idx));
    }

  g_hash_table_destroy(handle_to_idx);
}

FilterXScopeVariableLayout *
filterx_scope_variable_layout_new(FilterXExpr *root)
{
  FilterXScopeVariableLayout *self = g_new0(FilterXScopeVariableLayout, 1);

  if (!root)
    return self;

  LayoutBuilder b =
  {
    .handles_seen = g_hash_table_new(g_direct_hash, g_direct_equal),
    .exprs = g_ptr_array_new(),
  };

  if (filterx_expr_is_variable(root))
    _collect_variable(root, &b);
  filterx_expr_walk_children(root, _collect_variables_in_child_exprs, &b);

  _build_layout(self, &b);

  g_hash_table_destroy(b.handles_seen);
  g_ptr_array_unref(b.exprs);
  return self;
}

FilterXScopeVariableLayout *
filterx_scope_variable_layout_new_from_handles(FilterXVariableHandle *handles, gsize size)
{
  FilterXScopeVariableLayout *self = g_new0(FilterXScopeVariableLayout, 1);

  self->num_variables = size;

  if (self->num_variables == 0)
    return self;

  self->handles = g_new(FilterXVariableHandle, self->num_variables);
  memcpy(self->handles, handles, self->num_variables * sizeof(FilterXVariableHandle));

  qsort(self->handles, self->num_variables, sizeof(FilterXVariableHandle), _compare_handles);

  return self;
}

void
filterx_scope_variable_layout_free(FilterXScopeVariableLayout *self)
{
  if (!self)
    return;

  g_free(self->handles);
  g_free(self);
}
