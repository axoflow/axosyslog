/*
 * Copyright (c) 2025 Axoflow
 * Copyright (c) 2025 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#ifndef FILTERX_OBJECT_LIST_H
#define FILTERX_OBJECT_LIST_H

#include "filterx/filterx-sequence.h"

typedef struct _FilterXListObject
{
  FILTERX_MUTABLE_OBJECT_HEADER
  {
    FilterXSequence super;
    GPtrArray *array;
  }
  FILTERX_MUTABLE_OBJECT_TAILER;
} FilterXListObject;

G_STATIC_ASSERT(sizeof(FilterXListObject) == sizeof(FilterXMutableObject));

FILTERX_DECLARE_TYPE(list);

FilterXObject *filterx_list_new(void);
FilterXObject *filterx_list_new_from_syslog_ng_list(const gchar *repr, gssize repr_len);
FilterXObject *filterx_list_new_from_args(FilterXExpr *s, FilterXObject *args[], gsize args_len);

/* Devirtualized fast path for list subscript reads. Unwraps a FilterXRef wrapper and
 * dispatches directly to the list's get_subscript implementation, skipping the FilterXObject
 * vtable indirection. */
FilterXObject *filterx_list_get_subscript(FilterXObject *s, FilterXObject *key);

/* Devirtualized fast path for list subscript writes (by FilterXObject key, not by integer
 * index). Handles the FilterXRef cow + parent linkage inline. Named distinctly from the
 * by-index inline above. */
gboolean filterx_list_set_subscript_via_key(FilterXObject *s, FilterXObject *key, FilterXObject **new_value);


/* these are low-level, fast interfaces which bypass a lot of validations */

/* NOTE: index must be valid! */
static inline FilterXObject *
filterx_list_peek_subscript(FilterXObject *s, gint64 index)
{
  FilterXListObject *self = (FilterXListObject *) s;
  return g_ptr_array_index(self->array, index);
}

/* NOTE: index must be valid! */
static inline void
filterx_list_set_subscript(FilterXObject *s, gint64 index, FilterXObject **new_value)
{
  FilterXListObject *self = (FilterXListObject *) s;

  FilterXObject **slot = (FilterXObject **) &g_ptr_array_index(self->array, index);
  filterx_ref_unset_parent_container(*slot);
  filterx_object_unref(*slot);
  *slot = filterx_object_cow_store(new_value);
}


#endif
