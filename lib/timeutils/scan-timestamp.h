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

/* Application log timestamps.  Unlike scan_iso/pix/linksys/bsd_timestamp(),
 * these leave the cursor alone unless they match, so they can be tried one
 * after the other.
 * The time of day is H:MM:SS or HH:MM:SS with an optional fraction, a zone is
 * "+0200", "+02:00" or "Z", with or without a space before it. */

/* numeric dates with '-', '/' or '.' as the separator, year first or last:
 * "10-09-2026 15:31:50.456 +0000" (Splunk's own logs), "2026/10/09 15:31:50"
 * (nginx error log, glog), "10/09/2026 15:31:50", "09.10.2026 15:31:50".
 * With the year last, dots are read as day.month.year (European), slashes
 * and dashes as month/day/year (American), unless the first number cannot
 * be a month. */
gboolean scan_numeric_date_timestamp(const gchar **buf, gint *left, WallClockTime *wct);

/* "[Www, ]DD Mon YYYY HH:MM:SS[ zone]" with ' ', '-' or '/' between the date
 * parts and ' ' or ':' before the time, the month abbreviated or in full:
 * RFC 2822 ("Thu, 09 Oct 2026 15:31:50 +0200"), the access log of Apache
 * httpd and nginx ("09/Oct/2026:15:31:50 +0200"), Tomcat ("09-Oct-2026
 * 15:31:50.123") and "09 October 2026 15:31:50" */
gboolean scan_day_month_year_timestamp(const gchar **buf, gint *left, WallClockTime *wct);

/* the error log of Apache httpd: "Thu Oct 09 15:31:50.123456 2026" */
gboolean scan_apache_error_timestamp(const gchar **buf, gint *left, WallClockTime *wct);

/* java.util.logging: "Oct 09, 2026 3:31:50 PM", or a 24-hour clock without
 * the AM/PM, the month abbreviated or in full ("October 09, 2026 15:31:50") */
gboolean scan_java_util_logging_timestamp(const gchar **buf, gint *left, WallClockTime *wct);

gboolean scan_day_abbrev(const gchar **buf, gint *left, gint *wday);
gboolean scan_month_abbrev(const gchar **buf, gint *left, gint *mon);

#endif
