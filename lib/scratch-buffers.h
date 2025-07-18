/*
 * Copyright (c) 2017 Balabit
 * Copyright (c) 2017 Balazs Scheidler <balazs.scheidler@balabit.com>
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

#ifndef SCRATCH_BUFFERS2_H_INCLUDED
#define SCRATCH_BUFFERS2_H_INCLUDED 1

#include "syslog-ng.h"

typedef gint32 ScratchBuffersMarker;

GString *scratch_buffers_alloc(void);
GString *scratch_buffers_alloc_and_mark(ScratchBuffersMarker *marker);
void scratch_buffers_mark(ScratchBuffersMarker *marker);
void scratch_buffers_reclaim_allocations(void);
void scratch_buffers_reclaim_marked(ScratchBuffersMarker marker);

gssize scratch_buffers_get_global_allocation_count(void);
gssize scratch_buffers_get_local_allocation_count(void);
gssize scratch_buffers_get_local_allocation_bytes(void);
gint scratch_buffers_get_local_usage_count(void);
void scratch_buffers_update_stats(void);

/* lazy stats update */
void scratch_buffers_lazy_update_stats(void);

/* garbage collector */
void scratch_buffers_explicit_gc(void);

void scratch_buffers_allocator_init(void);
void scratch_buffers_allocator_deinit(void);
void scratch_buffers_automatic_gc_init(void);
void scratch_buffers_automatic_gc_deinit(void);

void scratch_buffers_register_stats(void);
void scratch_buffers_unregister_stats(void);


void scratch_buffers_global_init(void);
void scratch_buffers_global_deinit(void);

#endif
