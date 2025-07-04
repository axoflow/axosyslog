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

#ifndef ATOMIC_GSSIZE_H_INCLUDED
#define ATOMIC_GSSIZE_H_INCLUDED

#include "syslog-ng.h"

G_STATIC_ASSERT(sizeof(gssize) == sizeof(gpointer));

typedef struct
{
  gssize value;
} atomic_gssize;

static inline gssize
atomic_gssize_add(atomic_gssize *a, gssize add)
{
  return g_atomic_pointer_add(&a->value, add);
}

static inline gssize
atomic_gssize_sub(atomic_gssize *a, gssize sub)
{
  return g_atomic_pointer_add(&a->value, -1 * sub);
}

static inline gssize
atomic_gssize_inc(atomic_gssize *a)
{
  return g_atomic_pointer_add(&a->value, 1);
}

static inline gssize
atomic_gssize_dec(atomic_gssize *a)
{
  return g_atomic_pointer_add(&a->value, -1);
}

static inline gssize
atomic_gssize_get(atomic_gssize *a)
{
  return (gssize)g_atomic_pointer_get(&a->value);
}

static inline void
atomic_gssize_set(atomic_gssize *a, gssize value)
{
  g_atomic_pointer_set(&a->value, value);
}

static inline gsize
atomic_gssize_get_unsigned(atomic_gssize *a)
{
  return (gsize)g_atomic_pointer_get(&a->value);
}

static inline gssize
atomic_gssize_racy_get(atomic_gssize *a)
{
  return a->value;
}

static inline gsize
atomic_gssize_racy_get_unsigned(atomic_gssize *a)
{
  return (gsize)a->value;
}

static inline void
atomic_gssize_racy_set(atomic_gssize *a, gssize value)
{
  a->value = value;
}

static inline gsize
atomic_gssize_or(atomic_gssize *a, gsize value)
{
  return g_atomic_pointer_or(&a->value, value);
}

static inline gsize
atomic_gssize_xor(atomic_gssize *a, gsize value)
{
  return g_atomic_pointer_xor(&a->value, value);
}

static inline gsize
atomic_gssize_and(atomic_gssize *a, gsize value)
{
  return g_atomic_pointer_and(&a->value, value);
}

static inline gboolean
atomic_gssize_compare_and_exchange(atomic_gssize *a, gssize oldval, gssize newval)
{
  return g_atomic_pointer_compare_and_exchange(&a->value, oldval, newval);
}

static inline gssize
atomic_gssize_set_and_get(atomic_gssize *a, gssize value)
{
  gssize oldval = atomic_gssize_get(a);

  while (!atomic_gssize_compare_and_exchange(a,
                                             oldval,
                                             value))
    {
      oldval = atomic_gssize_get(a);
    }

  return oldval;
}
#endif
