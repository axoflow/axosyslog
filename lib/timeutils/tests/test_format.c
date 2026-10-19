/*
 * Copyright (c) 2026 Axoflow
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
#include <criterion/criterion.h>

#include "timeutils/format.h"
#include "timeutils/unixtime.h"
#include "apphook.h"

static gchar *
_format(gint64 sec, gint usec, glong gmtoff, gint ts_format, gint frac_digits)
{
  UnixTime ut = { .ut_sec = sec, .ut_usec = usec, .ut_gmtoff = gmtoff };
  GString *result = g_string_new("");

  append_format_unix_time(&ut, result, ts_format, gmtoff, frac_digits);
  return g_string_free(result, FALSE);
}

static void
_assert_format(gint64 sec, gint usec, glong gmtoff, gint ts_format, gint frac_digits, const gchar *expected)
{
  gchar *formatted = _format(sec, usec, gmtoff, ts_format, frac_digits);
  cr_assert_str_eq(formatted, expected);
  g_free(formatted);
}

Test(format, the_fraction_is_formatted_for_each_timestamp_within_the_same_second)
{
  _assert_format(1760877296, 123456, 0, TS_FMT_BSD, 3, "Oct 19 12:34:56.123");
  _assert_format(1760877296, 654321, 0, TS_FMT_BSD, 3, "Oct 19 12:34:56.654");
  _assert_format(1760877296, 654321, 0, TS_FMT_BSD, 0, "Oct 19 12:34:56");
  _assert_format(1760877296, 123456, 0, TS_FMT_ISO, 6, "2025-10-19T12:34:56.123456+00:00");
  _assert_format(1760877296, 7, 0, TS_FMT_ISO, 6, "2025-10-19T12:34:56.000007+00:00");
}

Test(format, a_new_second_is_formatted_again)
{
  _assert_format(1760877296, 0, 0, TS_FMT_ISO, 0, "2025-10-19T12:34:56+00:00");
  _assert_format(1760877297, 0, 0, TS_FMT_ISO, 0, "2025-10-19T12:34:57+00:00");
  _assert_format(1760877296, 0, 0, TS_FMT_ISO, 0, "2025-10-19T12:34:56+00:00");
}

Test(format, the_same_second_in_another_timezone_is_formatted_again)
{
  _assert_format(1760877296, 0, 0, TS_FMT_ISO, 0, "2025-10-19T12:34:56+00:00");
  _assert_format(1760877296, 0, 7200, TS_FMT_ISO, 0, "2025-10-19T14:34:56+02:00");
  _assert_format(1760877296, 0, -19800, TS_FMT_ISO, 3, "2025-10-19T07:04:56.000-05:30");
  _assert_format(1760877296, 0, 7200, TS_FMT_BSD, 0, "Oct 19 14:34:56");
  _assert_format(1760877296, 0, 0, TS_FMT_BSD, 0, "Oct 19 12:34:56");
}

Test(format, formats_are_cached_independently)
{
  for (gint i = 0; i < 2; i++)
    {
      _assert_format(1760877296, 0, 0, TS_FMT_BSD, 0, "Oct 19 12:34:56");
      _assert_format(1760877296, 0, 0, TS_FMT_ISO, 0, "2025-10-19T12:34:56+00:00");
      _assert_format(1760877296, 0, 0, TS_FMT_FULL, 0, "2025 Oct 19 12:34:56");
      _assert_format(1760877296, 250000, 0, TS_FMT_UNIX, 2, "1760877296.25");
    }
}

static void
setup(void)
{
  app_startup();
}

static void
teardown(void)
{
  app_shutdown();
}

TestSuite(format, .init = setup, .fini = teardown);
