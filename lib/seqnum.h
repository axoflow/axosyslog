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
#ifndef SEQNUM_H_INLCLUDED
#define SEQNUM_H_INLCLUDED 1

#include "syslog-ng.h"

static inline void
init_sequence_number(gint32 *seqnum)
{
  *seqnum = 1;
}

static inline gint32
step_sequence_number(gint32 *seqnum)
{
  gint32 old_value = *seqnum;
  (*seqnum)++;
  if (*seqnum < 0)
    *seqnum = 1;
  return old_value;
}

static inline gint32
step_sequence_number_atomic(gint32 *seqnum)
{
  return (gint32)g_atomic_int_add(seqnum, 1);
}


#endif
