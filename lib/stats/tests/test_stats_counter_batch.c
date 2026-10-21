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

#include "stats/stats-counter-batch.h"

static StatsCounterItem counter;
static StatsCounterItem other;

static void
_setup(void)
{
  memset(&counter, 0, sizeof(counter));
  memset(&other, 0, sizeof(other));
}

TestSuite(stats_counter_batch, .init = _setup);

Test(stats_counter_batch, updates_outside_of_a_batch_are_applied_immediately)
{
  stats_counter_inc_batched(&counter);
  stats_counter_add_batched(&counter, 5);
  cr_assert_eq(stats_counter_get(&counter), 6);
}

Test(stats_counter_batch, updates_in_a_batch_are_applied_at_its_end)
{
  stats_counter_batch_begin();
  stats_counter_inc_batched(&counter);
  stats_counter_inc_batched(&other);
  stats_counter_add_batched(&counter, 5);
  cr_assert_eq(stats_counter_get(&counter), 0);
  cr_assert_eq(stats_counter_get(&other), 0);
  stats_counter_batch_end();

  cr_assert_eq(stats_counter_get(&counter), 6);
  cr_assert_eq(stats_counter_get(&other), 1);
}

Test(stats_counter_batch, nested_batches_are_applied_at_the_end_of_the_outermost)
{
  stats_counter_batch_begin();
  stats_counter_batch_begin();
  stats_counter_inc_batched(&counter);
  stats_counter_batch_end();
  cr_assert_eq(stats_counter_get(&counter), 0);
  stats_counter_batch_end();

  cr_assert_eq(stats_counter_get(&counter), 1);
}

Test(stats_counter_batch, a_batch_with_many_counters_is_applied_completely)
{
  StatsCounterItem counters[40];

  memset(counters, 0, sizeof(counters));
  stats_counter_batch_begin();
  for (gint round = 0; round < 3; round++)
    for (gint i = 0; i < G_N_ELEMENTS(counters); i++)
      stats_counter_add_batched(&counters[i], i);
  stats_counter_batch_end();

  for (gint i = 0; i < G_N_ELEMENTS(counters); i++)
    cr_assert_eq(stats_counter_get(&counters[i]), 3 * i, "counter %d", i);
}

Test(stats_counter_batch, null_counters_are_ignored)
{
  stats_counter_inc_batched(NULL);
  stats_counter_batch_begin();
  stats_counter_inc_batched(NULL);
  stats_counter_inc_batched(&counter);
  stats_counter_batch_end();

  cr_assert_eq(stats_counter_get(&counter), 1);
}
