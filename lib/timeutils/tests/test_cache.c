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
#include "timeutils/misc.h"
#include "apphook.h"

#include <iv.h>

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

/* Verify that interleaving timestamps from the full +/-12h gmtoff range
 * does not thrash the cache: every hour offset should remain
 * independently cached across repeated alternation. */

#define MULTI_SLOT_HOURS 25  /* -12..+12 inclusive */

Test(test_cache, test_cached_gmtime_multi_slot)
{
  time_t base = get_cached_realtime_sec();
  time_t t[MULTI_SLOT_HOURS];
  struct tm ref[MULTI_SLOT_HOURS];

  for (gint h = 0; h < MULTI_SLOT_HOURS; h++)
    {
      t[h] = base + (h - 12) * 3600;
      gmtime_r(&t[h], &ref[h]);
    }

  for (gint i = 0; i < 10; i++)
    {
      for (gint h = 0; h < MULTI_SLOT_HOURS; h++)
        {
          struct tm tm;
          cached_gmtime(&t[h], &tm);
          cr_assert_eq(tm.tm_hour, ref[h].tm_hour);
          cr_assert_eq(tm.tm_min, ref[h].tm_min);
        }
    }
}

Test(test_cache, test_cached_localtime_multi_slot)
{
  time_t base = get_cached_realtime_sec();
  time_t t[MULTI_SLOT_HOURS];
  struct tm ref[MULTI_SLOT_HOURS];

  for (gint h = 0; h < MULTI_SLOT_HOURS; h++)
    {
      t[h] = base + (h - 12) * 3600;
      localtime_r(&t[h], &ref[h]);
    }

  for (gint i = 0; i < 10; i++)
    {
      for (gint h = 0; h < MULTI_SLOT_HOURS; h++)
        {
          struct tm tm;
          cached_localtime(&t[h], &tm);
          cr_assert_eq(tm.tm_hour, ref[h].tm_hour);
          cr_assert_eq(tm.tm_min, ref[h].tm_min);
        }
    }
}

Test(test_cache, test_cached_mktime_multi_slot)
{
  time_t base = get_cached_realtime_sec();
  struct tm tm[MULTI_SLOT_HOURS];
  time_t ref[MULTI_SLOT_HOURS];

  for (gint h = 0; h < MULTI_SLOT_HOURS; h++)
    {
      time_t when = base + (h - 12) * 3600;
      localtime_r(&when, &tm[h]);
      struct tm scratch = tm[h];
      ref[h] = mktime(&scratch);
    }

  for (gint i = 0; i < 10; i++)
    {
      for (gint h = 0; h < MULTI_SLOT_HOURS; h++)
        {
          struct tm scratch = tm[h];
          cr_assert_eq(cached_mktime(&scratch), ref[h]);
        }
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

/* The realtime cache's behaviour depends on the real clock, so these tests
 * run with app_startup() only, which initialises ivykis for the test thread
 * but never runs its loop, the same situation a threaded source worker is in. */

static void
setup_realtime(void)
{
  app_startup();
  cr_assert(iv_inited());
}

TestSuite(test_cache_realtime, .init = setup_realtime, .fini = teardown);

Test(test_cache_realtime, deferred_invalidation_keeps_the_value_until_the_event_loop_runs)
{
  struct timespec first, second;

  get_cached_realtime(&first);
  g_usleep(2000);
  get_cached_realtime(&second);
  cr_assert_eq(timespec_diff_nsec(&second, &first), 0,
               "the invalidation is deferred to an ivykis task, which never runs here");
}

Test(test_cache_realtime, disabled_realtime_cache_hits_the_clock_on_every_read)
{
  struct timespec first, second, third;

  get_cached_realtime(&first);
  disable_realtime_cache();

  /* the value cached before disabling is still handed out once */
  get_cached_realtime(&second);
  cr_assert_eq(timespec_diff_nsec(&second, &first), 0);

  g_usleep(2000);
  get_cached_realtime(&third);
  cr_assert_gt(timespec_diff_nsec(&third, &first), 0, "the read after the sleep must hit the clock again");
}
