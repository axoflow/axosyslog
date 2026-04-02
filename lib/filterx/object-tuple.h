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

#ifndef FILTERX_OBJECT_TUPLE_H
#define FILTERX_OBJECT_TUPLE_H

#include "filterx/filterx-sequence.h"

/* read only tuple of values */
typedef struct _FilterXTuple
{
  FilterXSequence super;
  GPtrArray *array;
} FilterXTuple;

FILTERX_DECLARE_TYPE(tuple);

FilterXObject *filterx_tuple_new(gsize len);
FilterXObject *filterx_tuple_new_from_args(FilterXExpr *s, FilterXObject *args[], gsize args_len);


/* these are low-level, fast interfaces which bypass a lot of validations */

/* NOTE: index must be valid! */
static inline FilterXObject *
filterx_tuple_peek_subscript(FilterXObject *s, gint64 index)
{
  FilterXTuple *self = (FilterXTuple *) s;
  return g_ptr_array_index(self->array, index);
}

static inline gsize
filterx_tuple_get_length(FilterXObject *s)
{
  FilterXTuple *self = (FilterXTuple *) s;

  return self->array->len;
}

/* NOTE: index must be valid! */
static inline void
filterx_tuple_set_subscript(FilterXObject *s, gint64 index, FilterXObject **new_value)
{
  FilterXTuple *self = (FilterXTuple *) s;

  if (self->array->len == index)
    g_ptr_array_set_size(self->array, index + 1);
  FilterXObject **slot = (FilterXObject **) &g_ptr_array_index(self->array, index);
  filterx_object_unref(*slot);
  *slot = *new_value;
}


#endif
