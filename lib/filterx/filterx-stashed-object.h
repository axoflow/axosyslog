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

FILTERX_DECLARE_TYPE(stash_reference);

/*
 * This object is used to thread-safely exchange a FilterXObject instance
 * under a running configuration.  For this to work, we do need an atomic
 * ref-counter in the object that we try to exchange, but FilterXObject
 * itself does not have an atomic refcount.
 *
 * This wrapper is just an atomic ref count plus an object reference and we
 * use instances of this structure to provide the object reference to
 * readers (e.g.  the running configuration).
 *
 * The encapsulated "object" is not locked in any way and may be accessed by
 * multiple threads, so in general it needs to be "frozen", as otherwise its
 * refcounts could become corrupt.
 */
typedef struct _FilterXStashedObject
{
  GAtomicCounter ref_cnt;
  FilterXEnvironment env;
  FilterXObject *object;
} FilterXStashedObject;

static inline FilterXStashedObject *
filterx_stashed_object_ref(FilterXStashedObject *self)
{
  g_atomic_counter_inc(&self->ref_cnt);
  return self;
}

static inline void
filterx_stashed_object_unref(FilterXStashedObject *self)
{
  if (self && g_atomic_counter_dec_and_test(&self->ref_cnt))
    {
      /* object does not need to be freed, as it was frozen, so will be
       * freed when the environment is freed */

      filterx_env_clear(&self->env);
      g_free(self);
    }
}

FilterXObject *filterx_stash_retrieve(FilterXStashedObject **stash);
gboolean filterx_stash_store(FilterXStashedObject **stash, FilterXObject *object, FilterXEnvironment *env);

#endif
