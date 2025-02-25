/*
 * Copyright (c) 2024 László Várady
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
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
};

#if SYSLOG_NG_ENABLE_DEBUG
static inline void
filterx_assert_not_ref(FilterXObject *object)
{
  g_assert(!_filterx_object_is_type(object, &FILTERX_TYPE_NAME(ref)));
}
#else
#define filterx_assert_not_ref(o)
#endif

void _filterx_ref_cow(FilterXRef *self);

/* Call them only where downcasting to a specific type is needed, the returned object should only be used locally. */
static inline FilterXObject *
filterx_ref_unwrap_ro(FilterXObject *s)
{
  if (!s || !(s->type == &FILTERX_TYPE_NAME(ref)))
    return s;

  FilterXRef *self = (FilterXRef *) s;
  return self->value;
}

static inline FilterXObject *
filterx_ref_unwrap_rw(FilterXObject *s)
{
  if (!s || !(s->type == &FILTERX_TYPE_NAME(ref)))
    return s;

  FilterXRef *self = (FilterXRef *) s;

  _filterx_ref_cow(self);

  return self->value;
}


FilterXObject *_filterx_ref_new(FilterXObject *value);

static inline FilterXObject *
filterx_ref_new(FilterXObject *value)
{
  if (!value || value->readonly || !_filterx_type_is_referenceable(value->type))
    return value;
  return _filterx_ref_new(value);
}

#endif
