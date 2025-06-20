/*
 * Copyright (c) 2024 László Várady
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

#ifndef FILTERX_REF_H
#define FILTERX_REF_H

#ifndef FILTERX_OBJECT_H_INCLUDED
#error "Please include filterx-ref.h through filterx-object.h"
#endif

#include "filterx/filterx-weakrefs.h"

/*
 * References are currently not part of the FilterX language (hopefully, they
 * never will be). FilterXRef is used to reference the same FilterXObject from
 * multiple locations (variables, other types of assignments) in order to
 * implement copy-on-write. For this reason, FilterXRef pretends to be a
 * FilterXObject, but it's not a real FilterX type.
 *
 * FilterXRef is a final class, do not inherit from it.
 *
 * The functionality behind FilterXRef and the locations where they are used are
 * open for extension.
 */

struct _FilterXRef
{
  FilterXObject super;
  FilterXObject *value;
  FilterXWeakRef parent_container;
};

gboolean _filterx_ref_cow_parents(FilterXObject *s, gpointer user_data);

static inline void
_filterx_ref_cow(FilterXRef *self)
{
  _filterx_ref_cow_parents(&self->super, NULL);
}

/* Call them only where downcasting to a specific type is needed, the returned object should only be used locally. */
static inline FilterXObject *
filterx_ref_unwrap_ro(FilterXObject *s)
{
  if (!s || !filterx_object_is_ref(s))
    return s;

  FilterXRef *self = (FilterXRef *) s;
  return self->value;
}

static inline FilterXObject *
filterx_ref_unwrap_rw(FilterXObject *s)
{
  if (!s || !filterx_object_is_ref(s))
    return s;

  FilterXRef *self = (FilterXRef *) s;

  _filterx_ref_cow(self);

  return self->value;
}

static inline gboolean
filterx_ref_values_equal(FilterXObject *r1, FilterXObject *r2)
{
  if (filterx_object_is_ref(r1))
    r1 = ((FilterXRef *) r1)->value;
  if (filterx_object_is_ref(r2))
    r2 = ((FilterXRef *) r2)->value;
  return r1 == r2;
}

static inline void
filterx_ref_set_parent_container(FilterXObject *s, FilterXObject *parent)
{
  if (filterx_object_is_ref(s))
    {
      FilterXRef *self = (FilterXRef *) s;

      g_assert(!parent || filterx_object_is_ref(parent));
      filterx_weakref_set(&self->parent_container, parent);
    }
}

static inline void
filterx_ref_unset_parent_container(FilterXObject *s)
{
  if (s && filterx_object_is_ref(s))
    {
      FilterXRef *self = (FilterXRef *) s;

      filterx_weakref_set(&self->parent_container, NULL);
    }
}


FilterXObject *_filterx_ref_new(FilterXObject *value);

#endif
