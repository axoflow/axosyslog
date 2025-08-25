/*
 * Copyright (c) 2025 Balázs Scheidler <balazs.scheidler@axoflow.com>
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
#include "libtest/fake-time.h"

#include "timeutils/cache.h"
#include "apphook.h"

Test(test_cache, test_cached_localtime_no_dst)
{
  time_t t = get_cached_realtime_sec();
  WallClockTime wct = WALL_CLOCK_TIME_INIT;

  cached_localtime_wct(&t, &wct);
  cr_assert_eq(wct.wct_gmtoff, 12*3600 + 45*60);
  for (gint i = 0; i < 15; i++)
    {
      t += 60;
      cached_localtime_wct(&t, &wct);
      cr_assert_eq(wct.wct_gmtoff, 12*3600 + 45*60);
    }
}

Test(test_cache, test_cached_localtime_dst)
{
  time_t t = get_cached_realtime_sec();
  WallClockTime wct = WALL_CLOCK_TIME_INIT;

  cached_localtime_wct(&t, &wct);
  cr_assert_eq(wct.wct_gmtoff, 12*3600 + 45*60);
  for (gint i = 0; i < 15; i++)
    {
      t += 60;
      cached_localtime_wct(&t, &wct);
      cr_assert_eq(wct.wct_gmtoff, 12*3600 + 45*60);
    }
}

void
setup(void)
{
  app_startup();
  setenv("TZ", "Pacific/Chatham", TRUE);
  tzset();

  /* Wed Aug 20 05:47:42 +1245 2025 */
  fake_time(1755622962);
}

void
teardown(void)
{
  app_shutdown();
}

TestSuite(test_cache, .init = setup, .fini = teardown);
