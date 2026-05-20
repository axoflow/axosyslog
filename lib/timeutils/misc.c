/*
 * Copyright (c) 2002-2013 Balabit
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

#include "compat/time.h"
#include "timeutils/misc.h"
#include "timeutils/cache.h"
#include "messages.h"

#include <errno.h>
#include <string.h>

/**
 * check_nanosleep:
 *
 * Check if nanosleep() is accurate enough for sub-millisecond sleeping. If
 * it is not good enough, we're skipping the minor sleeps in LogReader to
 * balance the cost of returning to the main loop (e.g.  we're always going
 * to return to the main loop, instead of trying to wait for the writer).
 **/
gboolean
check_nanosleep(void)
{
  struct timespec start, stop, sleep_amount;
  gint64 diff;
  gint attempts;

  for (attempts = 0; attempts < 3; attempts++)
    {
      clock_gettime(CLOCK_MONOTONIC, &start);
      sleep_amount.tv_sec = 0;
      /* 0.1 msec */
      sleep_amount.tv_nsec = 1e5;

      while (nanosleep(&sleep_amount, &sleep_amount) < 0)
        {
          if (errno != EINTR)
            return FALSE;
        }

      clock_gettime(CLOCK_MONOTONIC, &stop);
      diff = timespec_diff_nsec(&stop, &start);
      if (diff < 5e5)
        return TRUE;
    }
  return FALSE;
}

void
timespec_add_nsec(struct timespec *ts, gint64 nsec)
{
  ts->tv_sec += nsec / 1000000000L;
  nsec = nsec % 1000000000L;
  ts->tv_nsec += nsec;
  if (ts->tv_nsec >= 1000000000)
    {
      ts->tv_nsec -= 1000000000L;
      ts->tv_sec++;
    }
  else if (ts->tv_nsec < 0)
    {
      ts->tv_nsec += 1000000000L;
      ts->tv_sec--;
    }
}

void
timespec_add_msec(struct timespec *ts, gint64 msec)
{
  timespec_add_nsec(ts, MSEC_TO_NSEC(msec));
}

void
timespec_add_usec(struct timespec *ts, gint64 usec)
{
  timespec_add_nsec(ts, USEC_TO_NSEC(usec));
}

glong
timespec_diff_msec(const struct timespec *t1, const struct timespec *t2)
{
  return ((t1->tv_sec - t2->tv_sec) * 1000 + (t1->tv_nsec - t2->tv_nsec) / 1000000);
}

gint64
timespec_diff_usec(const struct timespec *t1, const struct timespec *t2)
{
  return (((gint64)t1->tv_sec - t2->tv_sec) * 1000000 + (t1->tv_nsec - t2->tv_nsec) / 1000);
}

gint64
timespec_diff_nsec(const struct timespec *t1, const struct timespec *t2)
{
  return (((gint64)t1->tv_sec - t2->tv_sec) * 1000000000) + (t1->tv_nsec - t2->tv_nsec);
}
