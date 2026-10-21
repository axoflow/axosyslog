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

#include "apphook.h"
#include "stats/stats-cluster-single.h"
#include "stats/aggregator/stats-aggregator-registry.h"

static StatsAggregator *
_register(void (*register_fn)(gint, StatsClusterKey *, StatsAggregator **), const gchar *name)
{
  StatsAggregator *aggr = NULL;
  StatsClusterKey key;

  stats_cluster_single_key_set(&key, name, NULL, 0);
  stats_aggregator_lock();
  register_fn(0, &key, &aggr);
  stats_aggregator_unlock();
  cr_assert_not_null(aggr);
  return aggr;
}

static void
_unregister(StatsAggregator *aggr)
{
  stats_aggregator_lock();
  stats_unregister_aggregator(&aggr);
  stats_aggregator_unlock();
}

Test(stats_aggregator, average_of_data_points_added_in_bulk)
{
  StatsAggregator *aggr = _register(stats_register_aggregator_average, "test_average");

  stats_aggregator_add_data_point(aggr, 10);
  stats_aggregator_add_data_points(aggr, 50, 2);
  cr_assert_eq(stats_counter_get(aggr->output_counter), 20);

  stats_aggregator_add_data_points(aggr, 0, 0);
  cr_assert_eq(stats_counter_get(aggr->output_counter), 20);

  stats_aggregator_add_data_points(aggr, 100, 1);
  cr_assert_eq(stats_counter_get(aggr->output_counter), 40);

  _unregister(aggr);
}

Test(stats_aggregator, data_points_added_in_bulk_count_as_their_mean_without_bulk_support)
{
  StatsAggregator *aggr = _register(stats_register_aggregator_maximum, "test_maximum");

  stats_aggregator_add_data_point(aggr, 10);
  stats_aggregator_add_data_points(aggr, 60, 2);
  cr_assert_eq(stats_counter_get(aggr->output_counter), 30);

  _unregister(aggr);
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

TestSuite(stats_aggregator, .init = setup, .fini = teardown);
