/*
 * Copyright (c) 2002-2012 Balabit
 * Copyright (c) 1998-2012 Balázs Scheidler
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

#ifndef ATOMIC_H_INCLUDED
#define ATOMIC_H_INCLUDED

#include "compat/glib.h"

typedef struct
{
  gint counter;
} GAtomicCounter;

static inline void
g_atomic_counter_inc(GAtomicCounter *c)
{
  g_atomic_int_inc(&c->counter);
}

static inline gboolean
g_atomic_counter_dec_and_test(GAtomicCounter *c)
{
  return g_atomic_int_dec_and_test(&c->counter);
}

static inline gint
g_atomic_counter_get(GAtomicCounter *c)
{
  return g_atomic_int_get(&c->counter);
}

static inline gint
g_atomic_counter_exchange_and_add(GAtomicCounter *c, gint val)
{
  return g_atomic_int_add(&c->counter, val);
}

static inline gint
g_atomic_counter_racy_get(GAtomicCounter *c)
{
  return c->counter;
}

static inline void
g_atomic_counter_set(GAtomicCounter *c, gint value)
{
  g_atomic_int_set(&c->counter, value);
}

#endif
