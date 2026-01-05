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

FilterXStashedObject *
filterx_stashed_object_ref(FilterXStashedObject *self)
{
  g_atomic_counter_inc(&self->ref_cnt);
  return self;
}

void
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

FilterXStashedObject *
filterx_stash_object(FilterXObject *object, FilterXEnvironment *env)
{
  FilterXStashedObject *self = g_new0(FilterXStashedObject, 1);
  g_atomic_counter_set(&self->ref_cnt, 1);

  filterx_env_move(&self->env, env);
  self->object = object;
  filterx_env_freeze_object(&self->env, &self->object);

  /* we don't need the weak_refs anymore, all objects, including those we
   * have weakrefs to are now persisted */
// FIXME:  g_ptr_array_unref(weak_refs);
  return self;
}
