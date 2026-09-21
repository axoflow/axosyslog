/*
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

#ifndef FILTERX_ALLOCATOR_H_INCLUDED
#define FILTERX_ALLOCATOR_H_INCLUDED

#include "syslog-ng.h"

/*
 * Allocator identity, recorded in every object allocated (see
 * FilterXObject.allocator_id): 0 is not an allocator at all but the heap
 * (g_malloc(), freed by refcount), the ids up to FILTERX_ALLOCATOR_ID_THREAD_MAX
 * name the per-thread arenas, and FILTERX_ALLOCATOR_ID_PRIVATE is shared by
 * every private arena (e.g.  the one a context snapshot owns): those never
 * meet each other legitimately, so telling them apart is not needed.
 */
#define FILTERX_ALLOCATOR_ID_BITS 11
#define FILTERX_ALLOCATOR_ID_HEAP 0
#define FILTERX_ALLOCATOR_ID_PRIVATE ((1 << FILTERX_ALLOCATOR_ID_BITS) - 1)
#define FILTERX_ALLOCATOR_ID_THREAD_MAX (FILTERX_ALLOCATOR_ID_PRIVATE - 1)

#define FILTERX_ALLOCATOR_DEFAULT_AREA_SIZE 65536
#define FILTERX_ALLOCATOR_MAX_ALLOC_SIZE 4096

typedef struct _FilterXArea
{
  gsize size, used;
  gchar mem[] __attribute__ ((aligned (16)));
} FilterXArea;

typedef struct _FilterXAllocator
{
  GPtrArray *areas;
  gint active_area;
  gint position_index;
  guint16 id;
  /* the size of the next area to create: doubles with each new area, up to
   * FILTERX_ALLOCATOR_DEFAULT_AREA_SIZE */
  gsize next_area_size;
} FilterXAllocator;

typedef struct _FilterXAllocatorPosition
{
  gint position_index;
  gint area;
  gsize area_used;
} FilterXAllocatorPosition;

void filterx_allocator_save_position(FilterXAllocator *allocator, FilterXAllocatorPosition *pos);
void filterx_allocator_restore_position(FilterXAllocator *allocator, FilterXAllocatorPosition *pos);
void filterx_allocator_empty(FilterXAllocator *allocator);

gpointer filterx_allocator_malloc(FilterXAllocator *allocator, gsize size, gsize zero_size);

/* the calling thread's arena: its id is derived from the thread, areas start
 * at the default size.  Idempotent, like before. */
void filterx_allocator_init(FilterXAllocator *allocator);
/* an arena of @id with areas starting at @initial_area_size bytes (doubling
 * from there); use FILTERX_ALLOCATOR_ID_PRIVATE for one that is not tied to a
 * thread */
void filterx_allocator_init_ext(FilterXAllocator *allocator, guint16 id, gsize initial_area_size);
void filterx_allocator_clear(FilterXAllocator *allocator);

static inline gboolean
filterx_allocator_alloc_size_supported(FilterXAllocator *allocator, gsize alloc_size)
{
  return alloc_size <= FILTERX_ALLOCATOR_MAX_ALLOC_SIZE;
}

#endif
