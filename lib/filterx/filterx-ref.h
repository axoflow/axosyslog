/*
 * Copyright (c) 2024-2026 László Várady
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

  /* NOTE: this pointer is only set on shadow xrefs. It identifies (but does
   * not reference) the xref we are shadowing for, so clone_container() can
   * properly identify which entry triggered the copy-on-write. */
  gpointer shadowed_xref;
};

gboolean _filterx_ref_cow_recurse(FilterXObject *s, gpointer user_data);
FilterXObject *_filterx_ref_new(FilterXObject *value);

static inline void
_filterx_ref_cow(FilterXRef *self)
{
  _filterx_ref_cow_recurse(&self->super, NULL);
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

static inline FilterXObject *
filterx_ref_add_inplace(FilterXObject *s, FilterXObject *o)
{
  FilterXRef *self = (FilterXRef *) s;
  return self->value->type->add_inplace(self->value, s, o);
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

static inline FilterXObject *
filterx_ref_float_unchecked(FilterXObject *s)
{
#if SYSLOG_NG_ENABLE_DEBUG
  g_assert(s->floating_ref == FALSE);
#endif
  s->floating_ref = TRUE;
  return s;
}

/* mark this xref as a floating one, not yet stored anywhere, can be stored
 * without cloning.  If @s is not an xref, do nothing, otherwise @s must be
 * a non-floating reference, you can't float a ref twice! */
static inline FilterXObject *
filterx_ref_float(FilterXObject *s)
{
  if (s && filterx_object_is_ref(s))
    {
      filterx_ref_float_unchecked(s);
    }
  return s;
}

/* TRUE iff the floating xref @s is the stand-in for the stored xref */
static inline gboolean
filterx_ref_shadows(FilterXObject *s, FilterXObject *stored)
{
  if (!s || !filterx_object_is_ref(s))
    return FALSE;

  FilterXRef *self = (FilterXRef *) s;
  return self->shadowed_xref == stored;
}

/* Creates a new shadow xref wrapping the same value as the stored xref
 * @original, marked as the stand-in for @original (see
 * filterx_ref_shadows()). */
static inline FilterXObject *
filterx_ref_create_shadow(FilterXObject *original)
{
  FilterXRef *orig = (FilterXRef *) original;
  FilterXObject *s = filterx_ref_float_unchecked(_filterx_ref_new(filterx_object_ref(orig->value)));
  ((FilterXRef *) s)->shadowed_xref = original;
  return s;
}

/* ground this xref (e.g.  make it not floating), the reverse of
 * filterx_ref_float().  This is to be used when the xref is stored
 * somewhere */
static inline FilterXObject *
filterx_ref_ground_unchecked(FilterXObject *s)
{
  FilterXRef *self = (FilterXRef *) s;

  s->floating_ref = FALSE;
  filterx_weakref_set(&self->parent_container, NULL);
  self->shadowed_xref = NULL;
  return s;
}

static inline FilterXObject *
filterx_ref_ground(FilterXObject *s)
{
  if (s && filterx_object_is_ref(s))
    {
      return filterx_ref_ground_unchecked(s);
    }
  return s;
}

FilterXObject *filterx_ref_dup(FilterXObject *s);

/* Given a child @s just read out of @c's underlying container, replace a shared child
 * xref with a floating one parented to @c so that a later mutation triggers copy-on-write
 * up the chain. This is the COW-critical step of the ref's getattr/get_subscript vtable;
 * the JIT-devirtualized dict/list read fast paths must call it after a raw (ref-bypassing)
 * lookup to preserve copy-on-write. @c must be a FilterXRef; @s may be any object (non-ref
 * children are returned unchanged). */
FilterXObject *filterx_ref_float_shared_child(FilterXObject *s, FilterXObject *c);

#endif
