/*
 * Copyright (c) 2002-2018 Balabit
 * Copyright (c) 1998-2018 Balázs Scheidler
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

#ifndef UNIXTIME_H_INCLUDED
#define UNIXTIME_H_INCLUDED

#include "timeutils/zoneinfo.h"
#include <stdint.h>

/*
 * This class represents a UNIX timestamp (as measured in time_t), the
 * fractions of a second (in microseconds) and an associated GMT timezone
 * offset.  In concept it is the combination of "struct timeval" and the
 * number of seconds compared to GMT.
 *
 * In concept, this is similar to WallClockTime, with the difference that
 * this represents the time in seconds since the UNIX epoch.
 *
 * In design, it is also similar to WallClockTime, the original intention
 * was to use an embedded "struct timeval", however that would enlarge the
 * LogMessage structure with a lot of padding (the struct would go from 16
 * to 24 bytes and we have 3 of these structs in LogMessage).
 */
typedef struct _UnixTime UnixTime;
struct _UnixTime
{
  gint64 ut_sec;
  guint32 ut_usec;

  /* zone offset in seconds, add this to UTC to get the time in local.  This
   * is just 32 bits, contrary to all other gmtoff variables, as we are
   * squeezed in space with this struct.  32 bit is more than enough for
   * +/-24*3600 */
  gint32 ut_gmtoff;
};

#define UNIX_TIME_INIT { -1, 0, -1 }

static inline gboolean
unix_time_is_set(const UnixTime *ut)
{
  return ut->ut_sec != -1;
}

static inline gboolean
unix_time_is_timezone_set(const UnixTime *self)
{
  return self->ut_gmtoff != -1;
}

void unix_time_unset(UnixTime *ut);
void unix_time_set_now(UnixTime *self);

void unix_time_set_timezone(UnixTime *self, gint new_gmtoff);
void unix_time_set_timezone_with_tzinfo(UnixTime *self, TimeZoneInfo *tzinfo);
void unix_time_fix_timezone(UnixTime *self, gint new_gmtoff);
void unix_time_fix_timezone_with_tzinfo(UnixTime *self, TimeZoneInfo *tzinfo);
gboolean unix_time_fix_timezone_assuming_the_time_matches_real_time(UnixTime *self);

gboolean unix_time_eq(const UnixTime *a, const UnixTime *b);
gint64 unix_time_diff_in_seconds(const UnixTime *a, const UnixTime *b);
gint64 unix_time_diff_in_msec(const UnixTime *a, const UnixTime *b);

struct timeval timeval_from_unix_time(UnixTime *ut);
UnixTime unix_time_from_unix_epoch_usec(guint64 unix_epoch);
guint64 unix_time_to_unix_epoch_usec(const UnixTime ut);
UnixTime unix_time_from_unix_epoch_nsec(guint64 unix_epoch_nsec);
guint64 unix_time_to_unix_epoch_nsec(const UnixTime ut);
UnixTime unix_time_add_duration(UnixTime time, guint64 duration);
void dump_unix_time(const UnixTime *ut, GString *output);

#endif
