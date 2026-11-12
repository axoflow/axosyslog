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
#include "scratch-buffers.h"
#include "stats/stats-registry.h"
#include "stats/stats-counter-batch.h"
#include "stats/stats-cluster-hist.h"
#include "stats/stats-cluster-single.h"
#include "stats/stats-prometheus.h"
#include "stats/aggregator/stats-aggregator-registry.h"

static StatsAggregator *
_register_histogram(const gchar *name)
{
  StatsAggregator *aggr = NULL;
  StatsClusterKey key;

  /* buckets: <=1, <=2, <=4, +Inf */
  stats_cluster_hist_key_set(&key, name, NULL, 0);
  stats_aggregator_lock();
  stats_register_aggregator_hist(0, &key, 0, 4, &aggr);
  stats_aggregator_unlock();
  cr_assert_not_null(aggr);
  return aggr;
}

static void
_unregister_histogram(StatsAggregator *aggr)
{
  stats_aggregator_lock();
  stats_unregister_aggregator(&aggr);
  stats_aggregator_unlock();
}

static StatsCluster *
_get_cluster(StatsAggregator *aggr)
{
  stats_lock();
  StatsCluster *sc = stats_get_cluster(&aggr->key);
  stats_unlock();
  cr_assert_not_null(sc);
  return sc;
}

static gsize
_get_value(StatsAggregator *aggr, gint type)
{
  return stats_counter_get(stats_cluster_get_counter(_get_cluster(aggr), type));
}

Test(stats_histogram, tracks_sum_count_maximum_and_buckets)
{
  StatsAggregator *aggr = _register_histogram("test_histogram");

  stats_aggregator_add_data_point(aggr, 3);
  stats_aggregator_add_data_point(aggr, 10);
  stats_aggregator_add_data_point(aggr, 1);

  cr_assert_eq(_get_value(aggr, SC_TYPE_HIST_COUNT), 3);
  cr_assert_eq(_get_value(aggr, SC_TYPE_HIST_SUM), 14);
  cr_assert_eq(_get_value(aggr, SC_TYPE_HIST_MAXIMUM), 10);
  cr_assert_eq(_get_value(aggr, SC_TYPE_HIST_BUCKET0), 1);
  cr_assert_eq(_get_value(aggr, SC_TYPE_HIST_BUCKET2), 1);
  cr_assert_eq(_get_value(aggr, SC_TYPE_HIST_BUCKET3), 1);

  stats_aggregator_reset(aggr);
  cr_assert_eq(_get_value(aggr, SC_TYPE_HIST_COUNT), 0);
  cr_assert_eq(_get_value(aggr, SC_TYPE_HIST_MAXIMUM), 0);

  stats_aggregator_add_data_point(aggr, 2);
  cr_assert_eq(_get_value(aggr, SC_TYPE_HIST_MAXIMUM), 2);

  _unregister_histogram(aggr);
}

Test(stats_histogram, counters_are_updated_at_the_end_of_a_counter_batch)
{
  StatsAggregator *aggr = _register_histogram("test_histogram");

  stats_counter_batch_begin();
  stats_aggregator_add_data_point(aggr, 3);
  stats_aggregator_add_data_point(aggr, 4);
  cr_assert_eq(_get_value(aggr, SC_TYPE_HIST_COUNT), 0);
  cr_assert_eq(_get_value(aggr, SC_TYPE_HIST_SUM), 0);
  cr_assert_eq(_get_value(aggr, SC_TYPE_HIST_BUCKET2), 0);
  cr_assert_eq(_get_value(aggr, SC_TYPE_HIST_MAXIMUM), 4, "the maximum is not batched");
  stats_counter_batch_end();

  cr_assert_eq(_get_value(aggr, SC_TYPE_HIST_COUNT), 2);
  cr_assert_eq(_get_value(aggr, SC_TYPE_HIST_SUM), 7);
  cr_assert_eq(_get_value(aggr, SC_TYPE_HIST_BUCKET2), 2);

  _unregister_histogram(aggr);
}

Test(stats_histogram, maximum_is_formatted_as_a_max_suffixed_series)
{
  StatsAggregator *aggr = _register_histogram("test_histogram");

  stats_aggregator_add_data_point(aggr, 7);

  StatsCluster *sc = _get_cluster(aggr);
  GString *record = stats_prometheus_format_counter(sc, SC_TYPE_HIST_MAXIMUM,
                                                    stats_cluster_get_counter(sc, SC_TYPE_HIST_MAXIMUM));
  cr_assert_str_eq(record->str, "syslogng_test_histogram_max 7\n");

  _unregister_histogram(aggr);
}

static gsize
_get_legacy_value(StatsClusterKey *key)
{
  stats_lock();
  StatsCluster *sc = stats_get_cluster(key);
  stats_unlock();
  cr_assert_not_null(sc);
  return stats_counter_get(stats_cluster_get_counter(sc, SC_TYPE_SINGLE_VALUE));
}

Test(stats_histogram, legacy_average_and_maximum_are_derived_from_the_histogram)
{
  StatsAggregator *aggr = NULL;
  StatsClusterKey key, average_key, maximum_key;

  stats_cluster_hist_key_set(&key, "test_histogram", NULL, 0);
  stats_cluster_single_key_legacy_set_with_name(&average_key, SCS_SOURCE | SCS_GROUP, "id", "instance", "msg_size_avg");
  stats_cluster_single_key_legacy_set_with_name(&maximum_key, SCS_SOURCE | SCS_GROUP, "id", "instance", "msg_size_max");

  stats_aggregator_lock();
  stats_register_aggregator_hist_with_legacy(0, &key, 0, 4, &average_key, &maximum_key, &aggr);
  stats_aggregator_unlock();
  cr_assert_not_null(aggr);

  stats_aggregator_add_data_point(aggr, 10);
  stats_aggregator_add_data_point(aggr, 30);
  cr_assert_eq(_get_legacy_value(&average_key), 0, "the legacy metrics are only updated by the aggregator timer");

  stats_aggregator_aggregate(aggr);
  cr_assert_eq(_get_legacy_value(&average_key), 20);
  cr_assert_eq(_get_legacy_value(&maximum_key), 30);

  stats_aggregator_reset(aggr);
  cr_assert_eq(_get_legacy_value(&average_key), 0);
  cr_assert_eq(_get_legacy_value(&maximum_key), 0);

  stats_aggregator_aggregate(aggr);
  cr_assert_eq(_get_legacy_value(&average_key), 0, "no division by zero without data points");

  _unregister_histogram(aggr);
}

static void
setup(void)
{
  app_startup();
}

static void
teardown(void)
{
  scratch_buffers_explicit_gc();
  app_shutdown();
}

TestSuite(stats_histogram, .init = setup, .fini = teardown);
