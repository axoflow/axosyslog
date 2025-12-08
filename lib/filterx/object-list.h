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
  FilterXSequence super;
  GPtrArray *array;
} FilterXListObject;

FILTERX_DECLARE_TYPE(list);

FilterXObject *filterx_list_new(void);
FilterXObject *filterx_list_new_from_syslog_ng_list(const gchar *repr, gssize repr_len);
FilterXObject *filterx_list_new_from_args(FilterXExpr *s, FilterXObject *args[], gsize args_len);


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
