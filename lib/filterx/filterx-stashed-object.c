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
#include "filterx/filterx-stashed-object.h"
#include "filterx/filterx-eval.h"
#include "filterx/object-null.h"

/* FilterXStashedReference:
 *
 * This is an object that ensures that stashed_objects are alive as long as
 * the current FilterXEvalContext is alive.  We basically store an instance
 * to this object as a weak reference and this will keep a reference to the
 * stashed object until its destructor is called.
 */
typedef struct _FilterXStashReference
{
  FilterXObject super;
  FilterXStashedObject *stashed_object;
} FilterXStashReference;

/* NOTE: consumes "stashed_object" reference */
static FilterXObject *
filterx_stash_reference_new(FilterXStashedObject *stashed_object)
{
  FilterXStashReference *self = filterx_new_object(FilterXStashReference);

  filterx_object_init_instance(&self->super, &FILTERX_TYPE_NAME(stash_reference));
  self->stashed_object = stashed_object;
  return &self->super;
}

static void
filterx_stash_reference_free(FilterXObject *s)
{
  FilterXStashReference *self = (FilterXStashReference *) s;

  filterx_stashed_object_unref(self->stashed_object);
  filterx_object_free_method(s);
}

/* we are deriving from null to satisfy asserts in filterx_type_init(), this
 * object is never surfaced to user code */
FILTERX_DEFINE_TYPE(stash_reference, FILTERX_TYPE_NAME(null),
  .free_fn = filterx_stash_reference_free,
);

/*
 * FilterXStashedObject
 *
 * This is a reference counted wrapper for stashed objects.  A stashed
 * object is a FilterXObject instance that multiple threads can access
 * (read-only!).  This mechanism is used by cached_json_file() to keep
 * loaded JSON representations around.
 *
 * The wrapped object is frozen and is guaranteed to be alive as long as the
 * FilterXStashedObject is alive.  Whenever the object is to be changed
 * (e.g.  JSON is reloaded), the following happens:
 *
 *   - threads currently using the "old" copy will keep a reference to the
 *     old stashed object (through FilterXStashedReference)
 *
 *   - a "new" FilterXStashedObject is created, and the atomic variable is
 *     updated to the new value (store-release).
 *
 *   - a new thread that ends up retrieving the stashed object will use the
 *     "new" copy (load-acquire).
 *
 *   - when all of the "old" using threads exit, the old stashed object is
 *     destroyed, eventually also destroying the stashed object as well.
 */
static FilterXStashedObject *
filterx_stashed_object_new(FilterXObject *object, FilterXEnvironment *env)
{
  FilterXStashedObject *self = g_new0(FilterXStashedObject, 1);
  g_atomic_counter_set(&self->ref_cnt, 1);

  filterx_env_move(&self->env, env);
  self->object = object;
  filterx_env_freeze_object(&self->env, &self->object);
  return self;
}

FilterXObject *
filterx_stash_retrieve(FilterXStashedObject **stash)
{
  if (!stash || !*stash)
    return NULL;
  /* load-acquire the atomic stash variable */

  /* g_atomic_pointer_get(): load */
  FilterXStashedObject *stashed_object = g_atomic_pointer_get(stash);

  /* filterx_stashed_object_ref(): acquire */
  FilterXObject *stashed_reference = filterx_stash_reference_new(filterx_stashed_object_ref(stashed_object));

  /* a weak_ref we store here makes sure that the "stashed_reference" is
   * alive as long as this FilterXEvalContext.  Once stashed_reference gets
   * freed, it releases the stashed_object ref, which in turn will release
   * any objects stored inside the stash.  All avoiding any locks, just the
   * use of atomics.  */
  filterx_eval_store_weak_ref(stashed_reference);
  filterx_object_unref(stashed_reference);

  /* this filterx_object_ref() is normally a noop as the object itself is
   * frozen, but is here as a matter of correctness. */
  return filterx_object_ref(stashed_object->object);
}

gboolean
filterx_stash_store(FilterXStashedObject **stash, FilterXObject *object, FilterXEnvironment *env)
{
  FilterXStashedObject *stashed_object = filterx_stashed_object_new(object, env);

  /* store-release the atomic stash variable */
  FilterXStashedObject *old_stashed_object = g_atomic_pointer_exchange(stash, stashed_object);
  filterx_stashed_object_unref(old_stashed_object);
  return TRUE;
}
