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

#ifndef TIMEUTILS_SCAN_TIMESTAMP_H_INCLUDED
#define TIMEUTILS_SCAN_TIMESTAMP_H_INCLUDED

#include "syslog-ng.h"
#include "timeutils/wallclocktime.h"
#include "timeutils/unixtime.h"

gboolean scan_iso_timezone(const guchar **buf, gint *length, gint *gmtoff);

gboolean scan_iso_timestamp(const gchar **buf, gint *left, WallClockTime *wct);
gboolean scan_pix_timestamp(const gchar **buf, gint *left, WallClockTime *wct);
gboolean scan_linksys_timestamp(const gchar **buf, gint *left, WallClockTime *wct);
gboolean scan_bsd_timestamp(const gchar **buf, gint *left, WallClockTime *wct);

gboolean scan_rfc3164_timestamp(const guchar **data, gint *length, WallClockTime *wct);
gboolean scan_rfc5424_timestamp(const guchar **data, gint *length, WallClockTime *wct);

gboolean scan_day_abbrev(const gchar **buf, gint *left, gint *wday);
gboolean scan_month_abbrev(const gchar **buf, gint *left, gint *mon);

#endif
