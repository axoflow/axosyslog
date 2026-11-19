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
#include "timeutils/scan-timestamp.h"
#include "timeutils/wallclocktime.h"
#include "str-format.h"
#include "str-utils.h"
#include "timeutils/cache.h"

#include <ctype.h>
#include <string.h>

gboolean
scan_day_abbrev(const gchar **buf, gint *left, gint *wday)
{
  *wday = -1;

  const gsize abbrev_length = 3;

  if (*left < abbrev_length)
    return FALSE;

  switch (**buf)
    {
    case 'S':
      if (strncasecmp(*buf, "Sun", abbrev_length) == 0)
        *wday = 0;
      else if (strncasecmp(*buf, "Sat", abbrev_length) == 0)
        *wday = 6;
      else
        return FALSE;
      break;
    case 'M':
      if (strncasecmp(*buf, "Mon", abbrev_length) == 0)
        *wday = 1;
      else
        return FALSE;
      break;
    case 'T':
      if (strncasecmp(*buf, "Tue", abbrev_length) == 0)
        *wday = 2;
      else if (strncasecmp(*buf, "Thu", abbrev_length) == 0)
        *wday = 4;
      else
        return FALSE;
      break;
    case 'W':
      if (strncasecmp(*buf, "Wed", abbrev_length) == 0)
        *wday = 3;
      else
        return FALSE;
      break;
    case 'F':
      if (strncasecmp(*buf, "Fri", abbrev_length) == 0)
        *wday = 5;
      else
        return FALSE;
      break;
    default:
      return FALSE;
    }

  (*buf) += abbrev_length;
  (*left) -= abbrev_length;
  return TRUE;
}

gboolean
scan_month_abbrev(const gchar **buf, gint *left, gint *mon)
{
  *mon = -1;

  const gsize abbrev_length = 3;

  if (*left < abbrev_length)
    return FALSE;

  switch (**buf)
    {
    case 'J':
      if (strncasecmp(*buf, "Jan", abbrev_length) == 0)
        *mon = 0;
      else if (strncasecmp(*buf, "Jun", abbrev_length) == 0)
        *mon = 5;
      else if (strncasecmp(*buf, "Jul", abbrev_length) == 0)
        *mon = 6;
      else
        return FALSE;
      break;
    case 'F':
      if (strncasecmp(*buf, "Feb", abbrev_length) == 0)
        *mon = 1;
      else
        return FALSE;
      break;
    case 'M':
      if (strncasecmp(*buf, "Mar", abbrev_length) == 0)
        *mon = 2;
      else if (strncasecmp(*buf, "May", abbrev_length) == 0)
        *mon = 4;
      else
        return FALSE;
      break;
    case 'A':
      if (strncasecmp(*buf, "Apr", abbrev_length) == 0)
        *mon = 3;
      else if (strncasecmp(*buf, "Aug", abbrev_length) == 0)
        *mon = 7;
      else
        return FALSE;
      break;
    case 'S':
      if (strncasecmp(*buf, "Sep", abbrev_length) == 0)
        *mon = 8;
      else
        return FALSE;
      break;
    case 'O':
      if (strncasecmp(*buf, "Oct", abbrev_length) == 0)
        *mon = 9;
      else
        return FALSE;
      break;
    case 'N':
      if (strncasecmp(*buf, "Nov", abbrev_length) == 0)
        *mon = 10;
      else
        return FALSE;
      break;
    case 'D':
      if (strncasecmp(*buf, "Dec", abbrev_length) == 0)
        *mon = 11;
      else
        return FALSE;
      break;
    default:
      return FALSE;
    }

  (*buf) += abbrev_length;
  (*left) -= abbrev_length;
  return TRUE;
}

/*******************************************************************************
 * RFC 3164 timestamp, expected format: "MMM DD HH:MM:SS" ...
 *******************************************************************************/

static gboolean
__is_bsd_rfc_3164(const guchar *src, guint32 left)
{
  return left >= 15 && src[3] == ' ' && src[6] == ' ' && src[9] == ':' && src[12] == ':';
}

gboolean
scan_bsd_timestamp(const gchar **buf, gint *left, WallClockTime *wct)
{
  if (!scan_month_abbrev(buf, left, &wct->wct_mon) ||
      !scan_expect_char(buf, left, ' ') ||
      !(scan_positive_int(buf, left, 2, &wct->wct_mday) ||
        scan_positive_int(buf, left, 1, &wct->wct_mday)) ||
      !scan_expect_char(buf, left, ' ') ||
      !scan_positive_int(buf, left, 2, &wct->wct_hour) ||
      !scan_expect_char(buf, left, ':') ||
      !scan_positive_int(buf, left, 2, &wct->wct_min) ||
      !scan_expect_char(buf, left, ':') ||
      !scan_positive_int(buf, left, 2, &wct->wct_sec))
    return FALSE;
  return TRUE;
}

/*******************************************************************************
 * RFC 3164 timestamp, with a single space between day and month "MMM DD
 * HH:MM:SS".  This only handles the case where the day is less than 10,
 * because the other would be handled by the normal BSD parsing code
 *******************************************************************************/

static gboolean
__is_bsd_rfc_3164_nopad_day(const guchar *src, guint32 left)
{
  if (left < 14)
    return FALSE;

  if (src[3] != ' ')
    return FALSE;

  if (!ch_isdigit(src[4]))
    return FALSE;

  return src[5] == ' ' && src[8] == ':' && src[11] == ':';
}

/*******************************************************************************
 * ISO timestamp as specified in RFC5424, expected format "YYYY-MM-DDTHH:MM:SS"
 *******************************************************************************/

static gboolean
__is_iso_stamp(const gchar *stamp, gint length)
{
  return (length >= 19
          && stamp[4] == '-'
          && stamp[7] == '-'
          && (stamp[10] == 'T' || stamp[10] == ' ')
          && stamp[13] == ':'
          && stamp[16] == ':'
         );
}

gboolean
scan_iso_timestamp(const gchar **buf, gint *left, WallClockTime *wct)
{
  if (!scan_positive_int(buf, left, 4, &wct->wct_year) ||
      !scan_expect_char(buf, left, '-') ||
      !scan_positive_int(buf, left, 2, &wct->wct_mon) ||
      !scan_expect_char(buf, left, '-') ||
      !scan_positive_int(buf, left, 2, &wct->wct_mday) ||
      !(scan_expect_char(buf, left, 'T') || scan_expect_char(buf, left, ' ')) ||
      !scan_positive_int(buf, left, 2, &wct->wct_hour) ||
      !scan_expect_char(buf, left, ':') ||
      !scan_positive_int(buf, left, 2, &wct->wct_min) ||
      !scan_expect_char(buf, left, ':') ||
      !scan_positive_int(buf, left, 2, &wct->wct_sec))
    return FALSE;
  wct->wct_year -= 1900;
  wct->wct_mon -= 1;
  return TRUE;
}

/*******************************************************************************
 * Cisco modified RFC3164 timestamp, expected format:
 *    "MMM DD YYYY HH:MM:SS:"
 *    "MMM DD YYYY HH:MM:SS "
 *******************************************************************************/

static gboolean
__is_bsd_pix_or_asa(const guchar *src, guint32 left)
{
  return (left >= 21
          && src[3] == ' '
          && src[6] == ' '
          && src[11] == ' '
          && src[14] == ':'
          && src[17] == ':'
          && (src[20] == ':' || src[20] == ' ')
          && ch_isdigit(src[7])
          && ch_isdigit(src[8])
          && ch_isdigit(src[9])
          && ch_isdigit(src[10])
         );
}

gboolean
scan_pix_timestamp(const gchar **buf, gint *left, WallClockTime *wct)
{
  if (!scan_month_abbrev(buf, left, &wct->wct_mon) ||
      !scan_expect_char(buf, left, ' ') ||
      !scan_positive_int(buf, left, 2, &wct->wct_mday) ||
      !scan_expect_char(buf, left, ' ') ||
      !scan_positive_int(buf, left, 4, &wct->wct_year) ||
      !scan_expect_char(buf, left, ' ') ||
      !scan_positive_int(buf, left, 2, &wct->wct_hour) ||
      !scan_expect_char(buf, left, ':') ||
      !scan_positive_int(buf, left, 2, &wct->wct_min) ||
      !scan_expect_char(buf, left, ':') ||
      !scan_positive_int(buf, left, 2, &wct->wct_sec))
    return FALSE;
  wct->wct_year -= 1900;
  return TRUE;
}

/*******************************************************************************
 * LinkSys modified RFC3164 timestamp, expected format:
 *    "MMM DD HH:MM:SS YYYY "
 *******************************************************************************/
static gboolean
__is_bsd_linksys(const guchar *src, guint32 left)
{
  /* "MMM DD HH:MM:SS YYYY " */
  return (left >= 21
          && src[3] == ' '
          && src[6] == ' '
          && src[9] == ':'
          && src[12] == ':'
          && src[15] == ' '
          && ch_isdigit(src[16])
          && ch_isdigit(src[17])
          && ch_isdigit(src[18])
          && ch_isdigit(src[19])
          && isspace(src[20])
         );
}

gboolean
scan_linksys_timestamp(const gchar **buf, gint *left, WallClockTime *wct)
{
  /* LinkSys timestamp, expected format: MMM DD HH:MM:SS YYYY */

  if (!scan_month_abbrev(buf, left, &wct->wct_mon) ||
      !scan_expect_char(buf, left, ' ') ||
      !scan_positive_int(buf, left, 2, &wct->wct_mday) ||
      !scan_expect_char(buf, left, ' ') ||
      !scan_positive_int(buf, left, 2, &wct->wct_hour) ||
      !scan_expect_char(buf, left, ':') ||
      !scan_positive_int(buf, left, 2, &wct->wct_min) ||
      !scan_expect_char(buf, left, ':') ||
      !scan_positive_int(buf, left, 2, &wct->wct_sec) ||
      !scan_expect_char(buf, left, ' ') ||
      !scan_positive_int(buf, left, 4, &wct->wct_year))
    return FALSE;
  wct->wct_year -= 1900;
  return TRUE;
}

/*******************************************************************************
 * Parse ISO timestamp
 *******************************************************************************/

static guint32
__parse_usec(const guchar **data, gint *length)
{
  guint32 usec = 0;
  const guchar *src = *data;

  /* Fractions of a second are in most cases using a dot, but some devices
   * use a comma (e.g.  Aruba) to separate the fractions of a seconds part */

  if (*length > 0 && (*src == '.' || *src == ','))
    {
      gulong frac = 0;
      gint div = 1;
      /* process second fractions */

      src++;
      (*length)--;
      while (*length > 0 && div < 10e5 && ch_isdigit(*src))
        {
          frac = 10 * frac + (*src) - '0';
          div = div * 10;
          src++;
          (*length)--;
        }
      while (*length > 0 && ch_isdigit(*src))
        {
          src++;
          (*length)--;
        }
      usec = frac * (1000000 / div);
    }
  *data = src;
  return usec;
}

static gboolean
__has_iso_timezone(const guchar *src, gint length)
{
  return (length >= 6) &&
         (*src == '+' || *src == '-') &&
         ch_isdigit(*(src+1)) &&
         ch_isdigit(*(src+2)) &&
         *(src+3) == ':' &&
         ch_isdigit(*(src+4)) &&
         ch_isdigit(*(src+5)) &&
         (length < 7 || !ch_isdigit(*(src+6)));
}

static guint32
__parse_iso_timezone(const guchar **data, gint *length)
{
  g_assert(*length >= 6);

  gint hours, mins;
  const guchar *src = *data;
  guint32 tz = 0;
  /* timezone offset */
  gint sign = *src == '-' ? -1 : 1;

  hours = (*(src + 1) - '0') * 10 + *(src + 2) - '0';
  mins = (*(src + 4) - '0') * 10 + *(src + 5) - '0';
  tz = sign * (hours * 3600 + mins * 60);

  src += 6;
  (*length) -= 6;

  *data = src;
  return tz;
}

gboolean
scan_iso_timezone(const guchar **data, gint *length, gint *gmtoff)
{
  if (__has_iso_timezone(*data, *length))
    {
      *gmtoff = __parse_iso_timezone(data, length);
      return TRUE;
    }
  return FALSE;
}

static gboolean
__parse_iso_stamp(WallClockTime *wct, const guchar **data, gint *length)
{
  /* RFC3339 timestamp, expected format: YYYY-MM-DDTHH:MM:SS[.frac]<+/->ZZ:ZZ */
  const guchar *src = *data;

  if (!scan_iso_timestamp((const gchar **) &src, length, wct))
    {
      return FALSE;
    }

  wct->wct_usec = __parse_usec(&src, length);

  if (*length > 0 && *src == 'Z')
    {
      /* Z is special, it means UTC */
      wct->wct_gmtoff = 0;
      src++;
      (*length)--;
    }
  else if (__has_iso_timezone(src, *length))
    {
      wct->wct_gmtoff = __parse_iso_timezone(&src, length);
    }
  else
    {
      wct->wct_gmtoff = -1;
    }

  *data = src;
  return TRUE;
}

/*******************************************************************************
 * Parse BSD timestamp
 *******************************************************************************/

static gboolean
__parse_bsd_timestamp(const guchar **data, gint *length, WallClockTime *wct)
{
  gint left = *length;
  const guchar *src = *data;

  if (__is_bsd_pix_or_asa(src, left))
    {
      if (!scan_pix_timestamp((const gchar **) &src, &left, wct))
        return FALSE;

      if (left && *src == ':')
        {
          src++;
          left--;
        }
    }
  else if (__is_bsd_linksys(src, left))
    {
      if (!scan_linksys_timestamp((const gchar **) &src, &left, wct))
        return FALSE;
    }
  else if (__is_bsd_rfc_3164(src, left) ||
           __is_bsd_rfc_3164_nopad_day(src, left))
    {
      if (!scan_bsd_timestamp((const gchar **) &src, &left, wct))
        return FALSE;

      wct->wct_usec = __parse_usec(&src, &left);

      wall_clock_time_guess_missing_year(wct);
    }
  else
    {
      return FALSE;
    }
  *data = src;
  *length = left;
  return TRUE;
}

gboolean
scan_rfc3164_timestamp(const guchar **data, gint *length, WallClockTime *wct)
{
  const guchar *src = *data;
  gint left = *length;

  /* If the next chars look like a date, then read them as a date. */
  if (__is_iso_stamp((const gchar *)src, left))
    {
      if (!__parse_iso_stamp(wct, &src, &left))
        return FALSE;
    }
  else
    {
      if (!__parse_bsd_timestamp(&src, &left, wct))
        return FALSE;
    }

  /* we might have a closing colon at the end of the timestamp, "Cisco" I am
   * looking at you, skip that as well, so we can reliably detect IPv6
   * addresses as hostnames, which would be using ":" as well. */

  if (left && *src == ':')
    {
      ++src;
      --left;
    }

  *data = src;
  *length = left;
  return TRUE;
}

gboolean
scan_rfc5424_timestamp(const guchar **data, gint *length, WallClockTime *wct)
{
  const guchar *src = *data;
  gint left = *length;

  if (!__parse_iso_stamp(wct, &src, &left))
    return FALSE;

  *data = src;
  *length = left;
  return TRUE;
}

/*******************************************************************************
 * Application log timestamps
 *
 * What applications write at the beginning of their log lines, as opposed to
 * the syslog formats above.  These scanners leave the caller's cursor alone
 * unless they match.
 *******************************************************************************/

static gboolean
_scan_digits(const gchar **buf, gint *left, gint min_digits, gint max_digits, gint *num, gint *n_digits)
{
  guint32 result = 0;
  gint n = 0;

  while (*left > 0 && n < max_digits && ch_isdigit(**buf))
    {
      result = result * 10 + (**buf - '0');
      (*buf)++;
      (*left)--;
      n++;
    }
  if (n < min_digits)
    return FALSE;

  *num = result;
  if (n_digits)
    *n_digits = n;
  return TRUE;
}

static gboolean
_set_date(WallClockTime *wct, gint year, gint mon, gint mday)
{
  if (mon < 1 || mon > 12 || mday < 1 || mday > 31)
    return FALSE;

  wct->wct_year = year - 1900;
  wct->wct_mon = mon - 1;
  wct->wct_mday = mday;
  return TRUE;
}

/* "Oct" or "October" */
static gboolean
_scan_month_name(const gchar **buf, gint *left, gint *mon)
{
  static const gchar *rest_of_name[] =
  { "uary", "ruary", "ch", "il", "", "e", "y", "ust", "tember", "ober", "ember", "ember" };

  if (!scan_month_abbrev(buf, left, mon))
    return FALSE;

  const gchar *rest = rest_of_name[*mon];
  gint rest_len = strlen(rest);

  if (rest_len && *left >= rest_len && strncasecmp(*buf, rest, rest_len) == 0)
    {
      *buf += rest_len;
      *left -= rest_len;
    }
  return TRUE;
}

/* H:MM:SS or HH:MM:SS, with an optional fraction */
static gboolean
_scan_time_of_day(const gchar **buf, gint *left, WallClockTime *wct)
{
  if (!_scan_digits(buf, left, 1, 2, &wct->wct_hour, NULL) ||
      !scan_expect_char(buf, left, ':') ||
      !_scan_digits(buf, left, 2, 2, &wct->wct_min, NULL) ||
      !scan_expect_char(buf, left, ':') ||
      !_scan_digits(buf, left, 2, 2, &wct->wct_sec, NULL))
    return FALSE;

  if (wct->wct_hour > 23 || wct->wct_min > 59 || wct->wct_sec > 60)
    return FALSE;

  wct->wct_usec = __parse_usec((const guchar **) buf, left);
  return TRUE;
}

/* "+0200", "+02:00" or "Z", with or without a space before it; nothing is
 * consumed when there is no zone */
static void
_scan_optional_zone(const gchar **buf, gint *left, WallClockTime *wct)
{
  const gchar *p = *buf;
  gint l = *left;

  if (l > 0 && *p == ' ')
    {
      p++;
      l--;
    }

  if (l > 0 && *p == 'Z')
    {
      wct->wct_gmtoff = 0;
      *buf = p + 1;
      *left = l - 1;
      return;
    }

  if (l >= 5 && (*p == '+' || *p == '-') && ch_isdigit(p[1]) && ch_isdigit(p[2]))
    {
      gint sign = *p == '-' ? -1 : 1;
      gint hours = (p[1] - '0') * 10 + (p[2] - '0');
      gint mins;

      if (ch_isdigit(p[3]) && ch_isdigit(p[4]))
        {
          mins = (p[3] - '0') * 10 + (p[4] - '0');
          p += 5;
          l -= 5;
        }
      else if (l >= 6 && p[3] == ':' && ch_isdigit(p[4]) && ch_isdigit(p[5]))
        {
          mins = (p[4] - '0') * 10 + (p[5] - '0');
          p += 6;
          l -= 6;
        }
      else
        return;

      wct->wct_gmtoff = sign * (hours * 3600 + mins * 60);
      *buf = p;
      *left = l;
    }
}

gboolean
scan_numeric_date_timestamp(const gchar **buf, gint *left, WallClockTime *wct)
{
  const gchar *p = *buf;
  gint l = *left;
  gint a, b, c, a_digits, c_digits;
  gint year, mon, mday;

  if (!_scan_digits(&p, &l, 1, 4, &a, &a_digits) || l < 1)
    return FALSE;

  gchar sep = *p;
  if (sep != '-' && sep != '/' && sep != '.')
    return FALSE;
  p++;
  l--;

  if (!_scan_digits(&p, &l, 1, 2, &b, NULL) ||
      !scan_expect_char(&p, &l, sep) ||
      !_scan_digits(&p, &l, 1, 4, &c, &c_digits))
    return FALSE;

  if (a_digits == 4 && c_digits <= 2)
    {
      /* year first: the month comes next, as in ISO 8601 */
      year = a;
      mon = b;
      mday = c;
    }
  else if (a_digits <= 2 && c_digits == 4)
    {
      /* year last: dots are European, slashes and dashes American, unless
       * the day gives the order away */
      year = c;
      if (sep == '.' || a > 12)
        {
          mday = a;
          mon = b;
        }
      else
        {
          mon = a;
          mday = b;
        }
    }
  else
    return FALSE;

  if (!_set_date(wct, year, mon, mday) ||
      !scan_expect_char(&p, &l, ' ') ||
      !_scan_time_of_day(&p, &l, wct))
    return FALSE;
  _scan_optional_zone(&p, &l, wct);

  *buf = p;
  *left = l;
  return TRUE;
}

gboolean
scan_day_month_year_timestamp(const gchar **buf, gint *left, WallClockTime *wct)
{
  const gchar *p = *buf;
  gint l = *left;
  gint wday = -1, mday, mon, year;

  /* RFC 2822 starts with the day of the week */
  if (scan_day_abbrev(&p, &l, &wday)
      && (!scan_expect_char(&p, &l, ',') || !scan_expect_char(&p, &l, ' ')))
    return FALSE;

  if (!_scan_digits(&p, &l, 1, 2, &mday, NULL) || l < 1)
    return FALSE;

  gchar sep = *p;
  if (sep != ' ' && sep != '-' && sep != '/')
    return FALSE;
  p++;
  l--;

  if (!_scan_month_name(&p, &l, &mon) ||
      !scan_expect_char(&p, &l, sep) ||
      !_scan_digits(&p, &l, 4, 4, &year, NULL))
    return FALSE;

  /* the access log of Apache httpd and nginx separates the time with a colon */
  if (l < 1 || (*p != ' ' && *p != ':'))
    return FALSE;
  p++;
  l--;

  if (!_set_date(wct, year, mon + 1, mday) ||
      !_scan_time_of_day(&p, &l, wct))
    return FALSE;
  _scan_optional_zone(&p, &l, wct);

  if (wday >= 0)
    wct->wct_wday = wday;
  *buf = p;
  *left = l;
  return TRUE;
}

gboolean
scan_apache_error_timestamp(const gchar **buf, gint *left, WallClockTime *wct)
{
  const gchar *p = *buf;
  gint l = *left;
  gint wday, mday, mon, year;

  if (!scan_day_abbrev(&p, &l, &wday) ||
      !scan_expect_char(&p, &l, ' ') ||
      !scan_month_abbrev(&p, &l, &mon) ||
      !scan_expect_char(&p, &l, ' ') ||
      !_scan_digits(&p, &l, 1, 2, &mday, NULL) ||
      !scan_expect_char(&p, &l, ' ') ||
      !_scan_time_of_day(&p, &l, wct) ||
      !scan_expect_char(&p, &l, ' ') ||
      !_scan_digits(&p, &l, 4, 4, &year, NULL))
    return FALSE;

  if (!_set_date(wct, year, mon + 1, mday))
    return FALSE;

  wct->wct_wday = wday;
  *buf = p;
  *left = l;
  return TRUE;
}

gboolean
scan_java_util_logging_timestamp(const gchar **buf, gint *left, WallClockTime *wct)
{
  const gchar *p = *buf;
  gint l = *left;
  gint mday, mon, year;

  if (!_scan_month_name(&p, &l, &mon) ||
      !scan_expect_char(&p, &l, ' ') ||
      !_scan_digits(&p, &l, 1, 2, &mday, NULL) ||
      !scan_expect_char(&p, &l, ',') ||
      !scan_expect_char(&p, &l, ' ') ||
      !_scan_digits(&p, &l, 4, 4, &year, NULL) ||
      !scan_expect_char(&p, &l, ' ') ||
      !_scan_time_of_day(&p, &l, wct))
    return FALSE;

  if (!_set_date(wct, year, mon + 1, mday))
    return FALSE;

  /* a 12-hour clock with AM/PM, a 24-hour one without */
  if (l >= 3 && p[0] == ' ' && (p[1] == 'A' || p[1] == 'P') && p[2] == 'M')
    {
      if (wct->wct_hour > 12)
        return FALSE;
      if (p[1] == 'P' && wct->wct_hour < 12)
        wct->wct_hour += 12;
      else if (p[1] == 'A' && wct->wct_hour == 12)
        wct->wct_hour = 0;
      p += 3;
      l -= 3;
    }

  *buf = p;
  *left = l;
  return TRUE;
}
