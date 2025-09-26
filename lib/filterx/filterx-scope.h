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
#ifndef FILTERX_SCOPE_H_INCLUDED
#define FILTERX_SCOPE_H_INCLUDED

#include "filterx-object.h"
#include "filterx-variable.h"
#include "logmsg/logmsg.h"

/*
 * FilterXScope represents variables in a filterx scope.
 *
 * Variables are either tied to a LogMessage (when we are caching
 * demarshalled values in the scope) or are values that are "floating", e.g.
 * not yet tied to any values in the underlying LogMessage.
 *
 * Floating values are "temp" values that are not synced to the LogMessage
 * upon the exit from the scope.
 *
 */

typedef struct _FilterXScope FilterXScope;
struct _FilterXScope
{
  guint16 write_protected:1, dirty:1, syncable:1, coupled_variables:1;
  FilterXGenCounter generation;
  LogMessage *msg;
  FilterXScope *parent_scope;
  struct
  {
    /* number of elems */
    gint len;
    /* allocated elems */
    gint size;
    union
    {
      FilterXVariable *separate;
      FilterXVariable coupled[0];
    };
  } variables;
};

typedef gboolean (*FilterXScopeForeachFunc)(FilterXVariable *variable, gpointer user_data);

void filterx_scope_sync(FilterXScope *self, LogMessage **pmsg, const LogPathOptions *path_options);

FilterXVariable *filterx_scope_lookup_variable(FilterXScope *self, FilterXVariableHandle handle);
FilterXVariable *filterx_scope_register_variable(FilterXScope *self,
                                                 FilterXVariableType variable_type,
                                                 FilterXVariableHandle handle);

/* variables and their objects must not be modified */
gboolean filterx_scope_foreach_variable_readonly(FilterXScope *self, FilterXScopeForeachFunc func, gpointer user_data);

gsize filterx_scope_get_alloc_size(void);
void filterx_scope_init_instance(FilterXScope *storage, gsize storage_size, FilterXScope *parent_scope);
void filterx_scope_clear(FilterXScope *self);
FilterXScope *filterx_scope_new(FilterXScope *parent_scope);
void filterx_scope_free(FilterXScope *self);

static inline void
filterx_scope_set_dirty(FilterXScope *self)
{
  self->dirty = TRUE;
}

static inline gboolean
filterx_scope_is_dirty(FilterXScope *self)
{
  return self->dirty;
}

static inline FilterXObject *
filterx_scope_get_variable(FilterXScope *self, FilterXVariable *v)
{
  return filterx_variable_get_value(v);
}

static inline void
filterx_scope_set_variable(FilterXScope *self, FilterXVariable *v, FilterXObject **value, gboolean assignment)
{
  g_assert(self->write_protected == FALSE);
  if (filterx_variable_is_floating(v))
    {
      G_STATIC_ASSERT(sizeof(v->generation) == sizeof(self->generation));
      filterx_variable_set_value(v, value, assignment, self->generation);
    }
  else
    {
      G_STATIC_ASSERT(sizeof(v->generation) == sizeof(self->msg->generation));
      filterx_variable_set_value(v, value, assignment, self->msg->generation);
    }
}

static inline void
filterx_scope_unset_variable(FilterXScope *self, FilterXVariable *v)
{
  g_assert(self->write_protected == FALSE);

  if (filterx_variable_is_floating(v))
    filterx_variable_unset_value(v, self->generation);
  else
    filterx_variable_unset_value(v, self->msg->generation);
}

static inline void
filterx_scope_set_message(FilterXScope *self, LogMessage *msg)
{
  if (msg == self->msg)
    return;
  if (self->msg)
    log_msg_unref(self->msg);
  self->msg = log_msg_ref(msg);
}

/* copy on write */
static inline void
filterx_scope_write_protect(FilterXScope *self)
{
  self->write_protected = TRUE;
}

static inline gboolean
filterx_scope_is_write_protected(FilterXScope *self)
{
  return self->write_protected;
}

static inline void
filterx_scope_make_writable(FilterXScope *self)
{
  g_assert(self->variables.len == 0);
  self->write_protected = FALSE;
}

static inline FilterXScope *
filterx_scope_reuse(FilterXScope *self)
{
  if (filterx_scope_is_write_protected(self))
    return NULL;

  self->generation++;
  return self;
}

#endif
