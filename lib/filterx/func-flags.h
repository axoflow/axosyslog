/*
 * Copyright (c) 2023 Axoflow
 * Copyright (c) 2024 shifter
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#ifndef FUNC_FLAGS_H
#define FUNC_FLAGS_H

#include "syslog-ng.h"

#define MAX_ENUMS 64

#define STATIC_ASSERT(COND) G_STATIC_ASSERT(COND)

#define DEFINE_FUNC_FLAGS(ENUM_NAME, ...) \
    typedef enum { __VA_ARGS__, ENUM_NAME##_MAX} ENUM_NAME; \
    STATIC_ASSERT(ENUM_NAME##_MAX <= MAX_ENUMS) \

#define FUNC_FLAGS_ITER(ENUM_NAME, CODE) for (guint64 enum_elt = 0; enum_elt < ENUM_NAME##_MAX; enum_elt++) { CODE; };

#define FLAG_VAL(ID) (1 << ID)

#define ALL_FLAG_SET(ENUM_NAME) (FLAG_VAL(ENUM_NAME##_MAX) - 1)

static inline void
set_flag(guint64 *flags, guint64 flag, gboolean val)
{
  if (val)
    {
      *flags |= FLAG_VAL(flag);
    }
  else
    {
      *flags &= ~FLAG_VAL(flag);
    }
}

static inline gboolean
check_flag(guint64 flags, guint64 flag)
{
  return (flags & FLAG_VAL(flag)) != 0;
}

static inline void
reset_flags(guint64 *flags, guint64 val)
{
  *flags = val;
}

static inline gboolean
toggle_flag(guint64 *flags, guint64 flag)
{
  *flags ^= FLAG_VAL(flag);
  return (*flags & FLAG_VAL(flag)) != 0;
}

#endif
