/*
 * Copyright (c) 2026 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#ifndef FILTERX_STASHED_OBJECT_H_INCLUDED
#define FILTERX_STASHED_OBJECT_H_INCLUDED 1

#include "filterx/filterx-object.h"
#include "filterx/filterx-env.h"

/*
 * This object is used to thread-safely exchange a FilterXObject instance
 * under a running configuration.  For this to work, we do need an atomic
 * ref-counter in the object that we try to exchange, but FilterXObject
 * itself does not need an atomic refcount.  It does at the moment, but it
 * is something I would like to change.
 *
 * This wrapper is just an atomic ref count plus an object reference and we
 * use instances of this structure to provide the object reference to
 * readers (e.g.  the running configuration).
 *
 * Readers:
 *   1) use g_atomic_pointer_get() to an instance of this and
 *   2) immediately take an (atomic) refcount before doing anything else
 *
 * Writers
 *   1) construct a new structure and replace the new instance with the old
 *      using g_atomic_pointer_exchange()
 *   2) the return value is the "old" instance, the first thing is to drop
 *      the (atomic) refcount, which might immediately cause the structure
 *      to be freed.
 *
 * The safety comes from the ordering requirements between the
 *   1) get value
 *   2) take ref
 *   3) exchange value
 *   4) drop ref
 *
 * Due to acquire (reader) <> acquire-release ordering (writer) the order of
 * these steps are ensured.
 *
 * The encapsulated "object" is not locked in any way and may be accessed by
 * multiple threads, so in general it needs to be "frozen", as otherwise its
 * refcounts could become corrupt.
 */
typedef struct _FilterXStashedObject
{
  /* has to be atomic to be exchanged using g_atomic_pointer_exchange() */
  GAtomicCounter ref_cnt;
  FilterXEnvironment env;
  FilterXObject *object;
} FilterXStashedObject;

FilterXStashedObject *filterx_stashed_object_ref(FilterXStashedObject *self);
void filterx_stashed_object_unref(FilterXStashedObject *self);

FilterXStashedObject *filterx_stash_object(FilterXObject *object, FilterXEnvironment *env);

#endif
