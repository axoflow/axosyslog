/*
 * Copyright (c) 2018 Balabit
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

#include "string.h"

#include "nvhandle-descriptors.h"
#include "syslog-ng.h"

NVHandleDescArray *
nvhandle_desc_array_new(guint reserved_size)
{
  NVHandleDescArray *self = g_new0(NVHandleDescArray, 1);
  self->data = g_new0(NVHandleDesc, reserved_size);
  self->allocated_len = reserved_size;
  self->old_buffers = g_ptr_array_new_with_free_func(g_free);
  return self;
}

void
nvhandle_desc_array_free(NVHandleDescArray *self)
{
  for (gint i=0; i < self->len; ++i)
    {
      NVHandleDesc *handle = (NVHandleDesc *)&self->data[i];
      nvhandle_desc_free(handle);
    }
  g_free(self->data);
  g_ptr_array_free(self->old_buffers, TRUE);
  g_free(self);
}

static void
nvhandle_desc_array_expand(NVHandleDescArray *self)
{
  guint new_alloc = self->allocated_len * 2;
  NVHandleDesc *new_data = g_new(NVHandleDesc, new_alloc);
  g_assert(new_data);

  memcpy(new_data, self->data, self->len * sizeof(NVHandleDesc));

  g_ptr_array_add(self->old_buffers, self->data);
  self->data = new_data;
  self->allocated_len = new_alloc;
}

/*
 * This function is called from the NVHandle allocation path, under the
 * protection of the nv_registry_lock.
 *
 * NOTE: the read side of `self->data` is not locked in any way.  The only
 * reason this works is that allocation must happen before any read side
 * would start using a specific handle.
 *
 * By the time a reader would start reading the `self->data` pointer,
 * `self->data` is either pointing to a new array (if another handle was
 * allocated and expansion was needed), or it is still pointing to an old
 * array (if expansion was not necessary).
 *
 * In either case, the reader dereferences a memory area that is kept around
 * in the `old_buffers` array until syslog-ng is terminated.
 *
 * While we could do an atomic update to `self->data` and `self->len`, the
 * read side actually:
 *
 *  1) never checks len, so changes and checks against len are always under
 *     the protection of the lock
 *  2) works both the old and the new values of data, so visibility is not a
 *     concern
 *  3) pointer writes are never torn (e.g.  we can't see values with
 *     upper/lower bits mixed from old/new values)
 *
 * The only assumption that the code must meet is that any handles used on
 * the read side must first be allocated.
 */
void
nvhandle_desc_array_append(NVHandleDescArray *self, NVHandleDesc *desc)
{
  if (self->len == self->allocated_len)
    nvhandle_desc_array_expand(self);

  self->data[self->len] = *desc;
  self->len++;
}

void
nvhandle_desc_free(NVHandleDesc *self)
{
  g_free(self->name);
}
