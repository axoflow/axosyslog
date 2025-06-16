/*
 * Copyright (c) 2023 László Várady <laszlo.varady93@gmail.com>
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

#ifndef STOPWATCH_H
#define STOPWATCH_H

#include "syslog-ng.h"
#include "compat/time.h"

typedef struct _Stopwatch
{
  struct timespec start_time;
} Stopwatch;

static inline void
stopwatch_start(Stopwatch *self)
{
  clock_gettime(CLOCK_MONOTONIC, &self->start_time);
}

static inline guint64
stopwatch_get_elapsed_nsec(Stopwatch *self)
{
  struct timespec stop_time;
  clock_gettime(CLOCK_MONOTONIC, &stop_time);

  return (stop_time.tv_sec - self->start_time.tv_sec) * 1000000000
         + (stop_time.tv_nsec - self->start_time.tv_nsec);
}

#endif
