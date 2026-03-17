/*
 * Copyright (c) 2002-2019 Balabit
 * Copyright (c) 1998-2019 Balázs Scheidler
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
#include "timeutils/conv.h"
#include "timeutils/cache.h"
#include "timeutils/misc.h"

void
convert_wall_clock_time_to_unix_time(const WallClockTime *src, UnixTime *dst)
{
  WallClockTime work_wct = *src;
  convert_and_normalize_wall_clock_time_to_unix_time(&work_wct, dst);
}

void
convert_wall_clock_time_to_unix_time_with_tz_hint(const WallClockTime *src, UnixTime *dst, long gmtoff_hint)
{
  WallClockTime work_wct = *src;
  convert_and_normalize_wall_clock_time_to_unix_time_with_tz_hint(&work_wct, dst, gmtoff_hint);
}

void
convert_and_normalize_wall_clock_time_to_unix_time(WallClockTime *src, UnixTime *dst)
{
  convert_and_normalize_wall_clock_time_to_unix_time_with_tz_hint(src, dst, -1);
}

/* hint the timezone value if it is not present in the wct struct, e.g.  the
 * timestamp takes precedence, but as an additional information the caller
 * can supply its best idea.  this maps nicely to the source-side
 * time-zone() value, which is only used in case an incoming timestamp does
 * not have the timestamp value. */
void
convert_and_normalize_wall_clock_time_to_unix_time_with_tz_hint(WallClockTime *src, UnixTime *dst, long gmtoff_hint)
{
  /* usec is just copied over, doesn't change timezone or anything */
  dst->ut_usec = src->wct_usec;

  /* determine target gmtoff if it's coming from the timestamp or from the hint */
  gint target_gmtoff = src->wct_gmtoff;
  if (target_gmtoff == -1)
    {
      target_gmtoff = gmtoff_hint;
      unix_time_set_timezone_source(dst, UNIX_TIME_TZ_ASSUMED);
    }

  /* FIRST: We convert the timestamp as it was in our local time zone. */
  gint unnormalized_hour = src->wct_hour;
  src->wct_isdst = -1;
  dst->ut_sec = cached_mktime_wct(src);
  gint normalized_hour = src->wct_hour;


  /* SECOND: adjust ut_sec as if we converted it according to our timezone. */
  gint local_gmtoff = get_local_timezone_ofs(dst->ut_sec);
  if (target_gmtoff == -1)
    {
      target_gmtoff = local_gmtoff;
      unix_time_set_timezone_source(dst, UNIX_TIME_TZ_ASSUMED);
    }
  dst->ut_sec = dst->ut_sec
                + local_gmtoff
                - (normalized_hour - unnormalized_hour) * 3600
                - target_gmtoff;

  dst->ut_gmtoff = target_gmtoff;
  src->wct_gmtoff = dst->ut_gmtoff;
  src->wct_hour = unnormalized_hour;
}

void
convert_unix_time_to_wall_clock_time(const UnixTime *src, WallClockTime *dst)
{
  convert_unix_time_to_wall_clock_time_with_tz_override(src, dst, -1);
}

/* the timezone information overrides what is present in the timestamp, e.g.
 * it will _convert_ the timestamp to a destination timezone */
void
convert_unix_time_to_wall_clock_time_with_tz_override(const UnixTime *src, WallClockTime *dst, gint gmtoff_override)
{
  gint gmtoff = gmtoff_override;

  if (gmtoff == -1)
    gmtoff = src->ut_gmtoff;
  if (gmtoff == -1)
    gmtoff = get_local_timezone_ofs(src->ut_sec);

  time_t t = src->ut_sec + gmtoff;
  cached_gmtime_wct(&t, dst);
  dst->wct_gmtoff = gmtoff;
  dst->wct_usec = src->ut_usec;

  /* NOTE: we are converting to a target timezone, but that timezone is
   * specified only via the gmtoff (and not via a timezone name, like CET).
   *
   * This means that in most cases we are unable to set wct_zone (which is
   * the timezone name) and neither can we set wct_isdst (which stores
   * whether we are in daylight saving).
   *
   * We could potentially set both in case we fall back to the local
   * timezone (e.g.  gmtoff_override is -1), but since we don't actually use
   * wct_zone and wct_isdst in our code, I didn't spend the time.  These
   * fields just mimic similar fields in struct tm (tm_zone and tm_isdst).
   */

  dst->wct_zone = NULL;
  dst->wct_isdst = -1;
}
