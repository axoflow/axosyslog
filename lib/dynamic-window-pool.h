/*
 * Copyright (c) 2002-2019 One Identity
 * Copyright (c) 2019 Laszlo Budai <laszlo.budai@balabit.com>
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

#ifndef DYNAMIC_WINDOW_POOL_H_INCLUDED
#define DYNAMIC_WINDOW_POOL_H_INCLUDED

#include "syslog-ng.h"
#include "atomic.h"

typedef struct _DynamicWindowPool DynamicWindowPool;

struct _DynamicWindowPool
{
  GAtomicCounter ref_cnt;

  gsize pool_size;
  gsize free_window;
  gsize balanced_window;
};

DynamicWindowPool *dynamic_window_pool_new(gsize iw_size);
void dynamic_window_pool_init(DynamicWindowPool *self);
DynamicWindowPool *dynamic_window_pool_ref(DynamicWindowPool *self);
void dynamic_window_pool_unref(DynamicWindowPool *self);

gsize dynamic_window_pool_request(DynamicWindowPool *self, gsize requested_size);
void dynamic_window_pool_release(DynamicWindowPool *self, gsize release_size);

#endif
