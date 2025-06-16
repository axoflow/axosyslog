/*
 * Copyright (c) 2002-2018 Balabit
 * Copyright (c) 2018 Laszlo Budai <laszlo.budai@balabit.com>
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

#ifndef WINDOW_SIZE_COUNTER_H_INCLUDED
#define WINDOW_SIZE_COUNTER_H_INCLUDED

#include "syslog-ng.h"
#include "atomic-gssize.h"

typedef struct _WindowSizeCounter WindowSizeCounter;

struct _WindowSizeCounter
{
  atomic_gssize counter;
};


void window_size_counter_set(WindowSizeCounter *c, gsize value);
gsize window_size_counter_get(WindowSizeCounter *c, gboolean *suspended);
gsize window_size_counter_add(WindowSizeCounter *c, gsize value, gboolean *suspended);
gsize window_size_counter_sub(WindowSizeCounter *c, gsize value, gboolean *suspended);
void window_size_counter_suspend(WindowSizeCounter *c);
void window_size_counter_resume(WindowSizeCounter *c);
gboolean window_size_counter_suspended(WindowSizeCounter *c);

gsize window_size_counter_get_max(void);

#endif

