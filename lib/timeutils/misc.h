/*
 * Copyright (c) 2002-2014 Balabit
 * Copyright (c) 1998-2010 Balázs Scheidler
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

#ifndef TIMEUTILS_MISC_H_INCLUDED
#define TIMEUTILS_MISC_H_INCLUDED

#include "syslog-ng.h"

#if ! defined(NSEC_PER_SEC)
# define NSEC_PER_SEC 1000000000L
# define USEC_PER_SEC 1000000L
# define MSEC_PER_SEC 1000L
# define NSEC_PER_USEC 1000L
#endif

#define MSEC_TO_NSEC(msec)   ((msec) * NSEC_PER_SEC / MSEC_PER_SEC)
#define MSEC_TO_USEC(msec)   ((msec) * USEC_PER_SEC / MSEC_PER_SEC)

#define USEC_TO_NSEC(usec)   ((usec) * NSEC_PER_SEC / USEC_PER_SEC)
#define NSEC_TO_USEC(nsec)   ((nsec) / NSEC_PER_USEC)

gboolean check_nanosleep(void);

void timespec_add_msec(struct timespec *ts, glong msec);
void timespec_add_usec(struct timespec *ts, gint64 usec);
glong timespec_diff_msec(const struct timespec *t1, const struct timespec *t2);
gint64 timespec_diff_usec(const struct timespec *t1, const struct timespec *t2);
gint64 timespec_diff_nsec(struct timespec *t1, struct timespec *t2);

#endif
