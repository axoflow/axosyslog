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
#include "filterx/filterx-scope.h"
#include "filterx/object-message-value.h"
#include "filterx/object-string.h"
#include "filterx/object-null.h"
#include "scratch-buffers.h"

typedef enum _FilterXVariablePullMode
{
  FX_VAR_PULL_COPY,
  FX_VAR_PULL_MOVE,
} FilterXVariablePullMode;

static gboolean
_search_variable(FilterXScope *self, FilterXVariableHandle handle, FilterXVariable **v_slot, gint *v_index)
{
  gint l, h, m;
  FilterXVariableHandle *handles = self->layout->handles;
  FilterXVariable *variables = self->variables;
  gint num_variables = (gint) self->variables_size;

  /* open-coded binary search */
  l = 0;
  h = num_variables - 1;
  while (l <= h)
    {
      m = (l + h) >> 1;

      FilterXVariableHandle mv = handles[m];

      if (mv == handle)
        {
          *v_slot = &variables[m];
          *v_index = m;
          return !!variables[m].variable_type;
        }
      else if (mv > handle)
        {
          h = m - 1;
        }
      else
        {
          l = m + 1;
        }
    }
  *v_slot = &variables[l];
  *v_index = l;
  return FALSE;
}

static inline gboolean
_validate_variable(FilterXScope *self, FilterXVariable *variable)
{
  if (filterx_variable_is_declared(variable))
    return TRUE;

  if (filterx_variable_is_floating(variable) &&
      !filterx_variable_is_same_generation(variable, self->generation))
    return FALSE;

  if (filterx_variable_is_message_tied(variable) &&
      !filterx_variable_is_same_generation(variable, self->msg->generation))
    return FALSE;
  return TRUE;
}

static FilterXVariable *
_pull_variable_from_parent_scope(FilterXScope *self, FilterXVariablePullMode pull_mode,
                                 FilterXVariable *parent_variable, FilterXVariable *output)
{
  *output = *parent_variable;
  self->variables_used++;
  if (output->value)
    {
      if (pull_mode == FX_VAR_PULL_COPY)
        output->value = filterx_object_copy(output->value);
      else if (pull_mode == FX_VAR_PULL_MOVE)
        {
          /* move variable without creating refs/copies from a scope that won't be ever used */
          parent_variable->value = NULL;
        }
    }

  if (pull_mode == FX_VAR_PULL_MOVE)
    parent_variable->variable_type = FX_VAR_NONE;

  msg_trace("Filterx scope, pulling scope variable",
            evt_tag_str("variable", log_msg_get_value_name((filterx_variable_get_nv_handle(output)), NULL)));
  return output;
}

static inline FilterXVariable *
_search_and_pull_from_parent_scopes(FilterXScope *self, FilterXVariableHandle handle, FilterXVariable *output)
{
  FilterXVariable *v;
  gint v_index;
  gboolean crossed_fork_point = FALSE;

  for (FilterXScope *scope = self->parent_scope; scope; scope = scope->parent_scope)
    {
      if (filterx_scope_is_fork_point(scope))
        crossed_fork_point = TRUE;

      if (_search_variable(scope, handle, &v, &v_index))
        {
          /* NOTE: we validate against @self */
          if (_validate_variable(self, v))
            {
              FilterXVariablePullMode pull_mode = crossed_fork_point ? FX_VAR_PULL_COPY : FX_VAR_PULL_MOVE;
              return _pull_variable_from_parent_scope(self, pull_mode, v, output);
            }
          else
            msg_trace("Filterx scope, not pulling stale scope variable",
                      evt_tag_str("variable", log_msg_get_value_name((filterx_variable_get_nv_handle(v)), NULL)));

          return NULL;
        }
    }

  return NULL;
}

FilterXVariable *
filterx_scope_lookup_variable(FilterXScope *self, FilterXVariableHandle handle, gint scope_var_idx)
{
  g_assert(scope_var_idx >= 0 && scope_var_idx < self->variables_size);

  FilterXVariable *v = &self->variables[scope_var_idx];

  if (G_UNLIKELY(!v->variable_type))
    return _search_and_pull_from_parent_scopes(self, handle, v);

  if (_validate_variable(self, v))
    return v;

  return NULL;
}

static FilterXObject *
_pull_variable_from_message(FilterXScope *self, NVHandle handle)
{
  return filterx_extract_object_from_logmsg(self->msg, handle);
}

FilterXVariable *
filterx_scope_register_variable(FilterXScope *self, FilterXVariableType variable_type, FilterXVariableHandle handle,
                                gint scope_var_idx)
{
  g_assert(scope_var_idx >= 0 && scope_var_idx < self->variables_size);

  FilterXVariable *v = &self->variables[scope_var_idx];
  filterx_variable_init_instance(v, variable_type, handle);
  self->variables_used++;

  /* the scope needs to be synced with the message if it holds a
   * message-tied variable (e.g.  $MSG) */
  if (variable_type == FX_VAR_MESSAGE_TIED)
    {
      FilterXObject *value = _pull_variable_from_message(self, filterx_variable_handle_to_nv_handle(handle));
      self->syncable = TRUE;

      /* NOTE: value may be NULL on an error, in that case the variable becomes an unset one */
      filterx_variable_set_value(v, &value, FALSE, self->msg->generation);
      filterx_object_unref(value);
    }
  else
    {
      filterx_variable_set_value(v, NULL, FALSE, self->generation);
    }
  return v;
}

gboolean
filterx_scope_foreach_variable_readonly(FilterXScope *self, FilterXScopeForeachFunc func, gpointer user_data)
{
  gboolean result = TRUE;
  GHashTable *already_found = g_hash_table_new(g_direct_hash, g_direct_equal);

  for (FilterXScope *scope = self; scope; scope = scope->parent_scope)
    {
      for (guint32 i = 0; i < scope->variables_size; i++)
        {
          FilterXVariable *variable = &scope->variables[i];

          if (!variable->variable_type || !variable->value)
            continue;

          /* NOTE: we validate against @self */
          if (!_validate_variable(self, variable))
            continue;

          if (g_hash_table_contains(already_found, GSIZE_TO_POINTER(variable->handle)))
            continue;

          /* objects are not pulled into the current scope (not cloned), so this is a readonly view */
          if (!func(variable, user_data))
            {
              result = FALSE;
              goto exit;
            }

          g_hash_table_add(already_found, GSIZE_TO_POINTER(variable->handle));
        }
    }

exit:
  g_hash_table_destroy(already_found);
  return result;
}

/*
 * 1) sync objects to message
 * 2) drop undeclared objects
 */
void
filterx_scope_sync(FilterXScope *self, LogMessage **pmsg, const LogPathOptions *path_options)
{
  filterx_scope_set_message(self, *pmsg);
  if (!self->dirty)
    {
      msg_trace("Filterx sync: not syncing as scope is not dirty",
                evt_tag_printf("scope", "%p", self));
      return;
    }
  if (!self->syncable)
    {
      msg_trace("Filterx sync: not syncing as there are no variables to sync",
                evt_tag_printf("scope", "%p", self));
      self->dirty = FALSE;
      return;
    }

  GString *buffer = NULL;

  gint msg_generation = self->msg->generation;

  for (guint32 i = 0; i < self->variables_size; i++)
    {
      FilterXVariable *v = &self->variables[i];
      if (!v->variable_type)
        continue;

      /* we don't need to sync the value if:
       *
       *  1) this is a floating variable; OR
       *
       *  2) the value was extracted from the message but was not changed in
       *     place (for mutable objects), and was not assigned to.
       *
       */
      if (filterx_variable_is_floating(v))
        {
          /* we could unset undeclared, floating values here as we are
           * transitioning to the next filterx block.  But this is also
           * addressed by the variable generation counter mechanism.  This
           * means that we will unset those if some expr actually refers to
           * it.  Also, we skip the entire sync exercise, unless we have
           * message tied values, so we need to work even if floating
           * variables are not synced.
           *
           * With that said, let's not clear these.
           */
        }
      else if (v->value == NULL)
        {
          if (log_msg_is_value_set(self->msg, filterx_variable_get_nv_handle(v)))
            {
              filterx_scope_set_message(self, log_msg_make_writable(pmsg, path_options));
              g_assert(v->generation == msg_generation);

              msg_trace("Filterx sync: whiteout variable, unsetting in message",
                        evt_tag_str("variable", log_msg_get_value_name(filterx_variable_get_nv_handle(v), NULL)));
              log_msg_unset_value(self->msg, filterx_variable_get_nv_handle(v));
            }

          filterx_variable_unassign(v);
          v->generation++;
        }
      else if (filterx_variable_is_assigned(v) || filterx_object_is_dirty(v->value))
        {
          LogMessageValueType t;

          if (!buffer)
            buffer = scratch_buffers_alloc();

          filterx_scope_set_message(self, log_msg_make_writable(pmsg, path_options));
          g_assert(v->generation == msg_generation);
          msg_trace("Filterx sync: changed variable in scope, overwriting in message",
                    evt_tag_str("variable", log_msg_get_value_name(filterx_variable_get_nv_handle(v), NULL)));

          g_string_truncate(buffer, 0);
          if (!filterx_object_marshal(v->value, buffer, &t))
            g_assert_not_reached();
          log_msg_set_value_with_type(self->msg, filterx_variable_get_nv_handle(v), buffer->str, buffer->len, t);
          filterx_object_set_dirty(v->value, FALSE);
          filterx_variable_unassign(v);
          v->generation++;
        }
      else
        {
          msg_trace("Filterx sync: variable in scope and message in sync, not doing anything",
                    evt_tag_str("variable", log_msg_get_value_name(filterx_variable_get_nv_handle(v), NULL)));
          v->generation++;
        }
    }
  /* FIXME: hack ! */
  self->msg->generation = msg_generation + 1;
  self->dirty = FALSE;
}

gsize
filterx_scope_get_alloc_size(FilterXScopeVariableLayout *layout)
{
  guint32 variables_size = layout ? layout->num_variables : 0;
  return sizeof(FilterXScope) + variables_size * sizeof(FilterXVariable);
}

void
filterx_scope_init_instance(FilterXScope *storage, gsize storage_size, FilterXScope *parent_scope,
                            FilterXScopeVariableLayout *layout)
{
  gsize alloc_size = filterx_scope_get_alloc_size(layout);
  g_assert(storage_size >= alloc_size);

  FilterXScope *self = storage;
  memset(self, 0, alloc_size);
  self->layout = layout;
  self->variables_size = layout ? layout->num_variables : 0;

  if (parent_scope)
    {
      self->parent_scope = parent_scope;
      self->generation = parent_scope->generation + 1;
      if (parent_scope->variables_used > 0)
        self->dirty = parent_scope->dirty;
      self->syncable = parent_scope->syncable;
      self->msg = log_msg_ref(parent_scope->msg);
    }
}

void
filterx_scope_clear(FilterXScope *self)
{
  if (self->msg)
    log_msg_unref(self->msg);

  for (guint32 i = 0; i < self->variables_size; i++)
    filterx_variable_clear(&self->variables[i]);
}

FilterXScope *
filterx_scope_new(FilterXScope *parent_scope, FilterXScopeVariableLayout *layout)
{
  gsize alloc_size = filterx_scope_get_alloc_size(layout);
  FilterXScope *self = g_malloc(alloc_size);

  filterx_scope_init_instance(self, alloc_size, parent_scope, layout);
  return self;
}

void
filterx_scope_free(FilterXScope *self)
{
  filterx_scope_clear(self);
  g_free(self);
}
