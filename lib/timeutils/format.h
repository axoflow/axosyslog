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
#ifndef TIMEUTILS_FORMAT_H_INCLUDED
#define TIMEUTILS_FORMAT_H_INCLUDED

#include "unixtime.h"
#include "wallclocktime.h"

/* timestamp formats */
#define TS_FMT_BSD   0
#define TS_FMT_ISO   1
#define TS_FMT_FULL  2
#define TS_FMT_UNIX  3

void format_unix_time(const UnixTime *stamp, GString *target,
                      gint ts_format, glong zone_offset, gint frac_digits);
void append_format_unix_time(const UnixTime *stamp, GString *target,
                             gint ts_format, glong zone_offset, gint frac_digits);
void format_wall_clock_time(const WallClockTime *stamp, GString *target,
                            gint ts_format, glong zone_offset, gint frac_digits);
void append_format_wall_clock_time(const WallClockTime *stamp, GString *target,
                                   gint ts_format, gint frac_digits);
void append_format_zone_info(GString *target, glong gmtoff);

#endif
