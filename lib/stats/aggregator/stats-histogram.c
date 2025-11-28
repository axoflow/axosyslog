/*
 * Copyright (c) 2025 Balazs Scheidler <balazs.scheidler@axoflow.com>
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
#include "stats/aggregator/stats-aggregator.h"
#include "stats/stats-registry.h"
#include "stats/stats-cluster-hist.h"
#include "stats/stats-prometheus.h"
#include "compat/pow2.h"

typedef struct
{
  StatsAggregator super;
  gint min_bucket, num_buckets;
  StatsCounterItem *sum;
  StatsCounterItem *count;
  StatsCounterItem *buckets[SC_TYPE_HIST_MAX_BUCKETS];
} StatsAggregatorHistogram;

static void
_register_aggr(StatsAggregator *s)
{
  StatsAggregatorHistogram *self = (StatsAggregatorHistogram *) s;

  stats_lock();
  stats_register_counter(s->stats_level, &s->key, SC_TYPE_HIST_SUM, &self->sum);
  stats_register_counter(s->stats_level, &s->key, SC_TYPE_HIST_COUNT, &self->count);
  for (gint i = 0; i < self->num_buckets; i++)
    stats_register_counter(s->stats_level, &s->key, SC_TYPE_HIST_BUCKET0+i, &self->buckets[i]);
  stats_unlock();
}

static void
_unregister_aggr(StatsAggregator *s)
{
  StatsAggregatorHistogram *self = (StatsAggregatorHistogram *) s;

  stats_lock();
  stats_unregister_counter(&s->key, SC_TYPE_HIST_SUM, &self->sum);
  stats_unregister_counter(&s->key, SC_TYPE_HIST_COUNT, &self->count);
  for (gint i = 0; i < self->num_buckets; i++)
    stats_unregister_counter(&s->key, SC_TYPE_HIST_BUCKET0+i, &self->buckets[i]);
  stats_unlock();
}

static void
_add_data_point(StatsAggregator *s, gsize value)
{
  StatsAggregatorHistogram *self = (StatsAggregatorHistogram *) s;

  stats_counter_inc(self->count);
  stats_counter_add(self->sum, value);

  gssize bucket = (round_to_log2(value));

  /* anything below min_bucket goes to the first bucket */
  bucket -= self->min_bucket;
  if (bucket < 0)
    bucket = 0;

  if (bucket >= self->num_buckets)
    bucket = self->num_buckets - 1;

  stats_counter_inc(self->buckets[bucket]);
}

static void
_reset(StatsAggregator *s)
{
  StatsAggregatorHistogram *self = (StatsAggregatorHistogram *) s;

  for (gint i = 0; i < self->num_buckets; i++)
    stats_counter_set(self->buckets[i], 0);
  stats_counter_set(self->count, 0);
  stats_counter_set(self->sum, 0);
}

static void
_override_bucket_names(StatsAggregatorHistogram *self)
{
  GPtrArray *bucket_names = g_ptr_array_new();
  g_ptr_array_add(bucket_names, g_strdup("sum"));
  g_ptr_array_add(bucket_names, g_strdup("count"));

  for (gint i = 0; i < self->num_buckets - 1; i++)
    {
      const gchar *v =
        stats_format_prometheus_format_value(self->super.key.formatting.stored_unit,
                                             SCFOR_NONE, pow2(i+self->min_bucket));
      g_ptr_array_add(bucket_names, g_strdup(v));
    }
  g_ptr_array_add(bucket_names, g_strdup("+Inf"));
  g_ptr_array_add(bucket_names, NULL);

  self->super.key.counter_group_init.counter.names = (gchar **) g_ptr_array_free(bucket_names, FALSE);
}

StatsAggregator *
stats_aggregator_histogram_new(gint level, StatsClusterKey *sc_key, gint min_bucket, gint num_buckets)
{
  StatsAggregatorHistogram *self = g_new0(StatsAggregatorHistogram, 1);
  stats_aggregator_init_instance(&self->super, sc_key, level);

  self->super.register_aggr = _register_aggr;
  self->super.unregister_aggr = _unregister_aggr;
  self->super.add_data_point = _add_data_point;
  self->super.reset = _reset;

  self->min_bucket = min_bucket;
  self->num_buckets = num_buckets;
  _override_bucket_names(self);

  return &self->super;
}
