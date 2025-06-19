/*
 * Copyright (c) 2014 Balabit
 * Copyright (c) 2014 Gergely Nagy <algernon@madhouse-project.org>
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

#ifndef COMPAT_TIME_H_INCLUDED
#define COMPAT_TIME_H_INCLUDED

#include "compat/compat.h"
#include <time.h>

#if !defined(SYSLOG_NG_HAVE_CLOCK_GETTIME) && defined(__APPLE__) && defined(__MACH__)

#include <mach/clock.h>
#include <mach/mach.h>

#define CLOCK_REALTIME CALENDAR_CLOCK
#define CLOCK_MONOTONIC SYSTEM_CLOCK

int clock_gettime(clock_t clock_id, struct timespec *timestamp);

#else

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC CLOCK_REALTIME
#endif

#endif /* !SYSLOG_NG_HAVE_CLOCK_GETTIME && __APPLE__ && __MACH__ */

#endif /* COMPAT_TIME_H_INCLUDED */
