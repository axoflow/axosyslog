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

typedef struct _FilterXAllocator
{
  GPtrArray *areas;
  gint active_area;
  gint position_index;
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

void filterx_allocator_init(FilterXAllocator *allocator);
void filterx_allocator_clear(FilterXAllocator *allocator);

static inline gboolean
filterx_allocator_alloc_size_supported(FilterXAllocator *allocator, gsize alloc_size)
{
  return alloc_size <= 4096;
}

#endif
