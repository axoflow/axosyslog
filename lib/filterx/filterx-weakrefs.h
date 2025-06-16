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
#ifndef FILTERX_WEAKREFS_H_INCLUDED
#define FILTERX_WEAKREFS_H_INCLUDED

#ifndef FILTERX_OBJECT_H_INCLUDED
#error "Please include weakrefs from filterx-object.h"
#endif

typedef struct _FilterXWeakRef
{
  FilterXObject *object;
} FilterXWeakRef;

typedef gboolean (*FilterXWeakRefInvokeFunc)(FilterXObject *s, gpointer user_data);

/*
 * Weakrefs can be used to break up circular references between objects
 * without having to do expensive loop discovery.  Although we are not
 * implementing complete weakref support, the interface mimics that, so we
 * could eventually implement it.
 *
 * Weakrefs work as follows:
 *   - each object can have a combination of hard refs and weak refs
 *   - the object can be freed when the hard references are gone
 *   - all weak refs will be cleared (e.g. set to NULL) when the object is freed
 *
 * This means that if we have circular references between a objects (not
 * necessarily two), the circle can be broken by just one weakref in the loop.
 *
 * Our weakref implementation is limited, as we don't have a GC mechanism.
 * We will destroy lingering objects at the end of our current scope and
 * weakrefs don't outlive scopes.
 *
 * With this assumption, our end-of-scope disposal of objects will work like this:
 *   - we get rid of all weakrefs
 *   - we get rid of all hardrefs
 *
 * Which should just get rid of all loops.  We currently _do not_ set the
 * weakref back to NULL at disposal, since no code is to be executed _after_
 * the disposal is done, e.g.  the invalidated pointers should not be
 * dereferenced.
 */

void filterx_weakref_set(FilterXWeakRef *self, FilterXObject *object);

static inline void
filterx_weakref_clear(FilterXWeakRef *self)
{
  /* we do nothing with our ref here as that should be disposed at the end of the scope automatically */
  self->object = NULL;
}

/* NOTE: returns a hard reference if the weakref is still valid */
static inline FilterXObject *
filterx_weakref_get(FilterXWeakRef *self)
{
  /* deref is now just returning the pointer, we don't have a means to
   * validate if it's valid, we just assume it is until our Scope  has not
   * been terminated yet */
  return filterx_object_ref(self->object);
}

/*
 * This function avoids taking a ref, at least compared to weakref_get(), do
 * something, unref, as it can build on the implementation details of FilterXWeakRef.
 */
static inline gboolean
filterx_weakref_invoke(FilterXWeakRef *self, FilterXWeakRefInvokeFunc func, gpointer user_data)
{
  if (self->object)
    return func(self->object, user_data);
  return FALSE;
}

static inline gboolean
filterx_weakref_is_set(FilterXWeakRef *self)
{
  return self->object != NULL;
}

static inline gboolean
filterx_weakref_is_set_to(FilterXWeakRef *self, FilterXObject *object)
{
  return self->object == object;
}

static inline void
filterx_weakref_copy(FilterXWeakRef *self, const FilterXWeakRef *other)
{
  /* other is already in a weakref, no need to store it in the weakrefs array */
  self->object = other->object;
}

#endif
