/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2023-2024 Attila Szakacs <attila.szakacs@axoflow.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#include <criterion/criterion.h>
#include "stats/stats-cluster-single.h"
#include "stats/stats-registry.h"


static inline gboolean
metrics_probe_test_stats_cluster_exists(const gchar *key, StatsClusterLabel *labels, gsize labels_len)
{
  StatsCluster *cluster = NULL;

  StatsClusterKey sc_key;
  stats_cluster_single_key_set(&sc_key, key, labels, labels_len);

  stats_lock();
  {
    cluster = stats_get_cluster(&sc_key);
  }
  stats_unlock();

  return cluster != NULL;
}

static inline gsize
metrics_probe_test_get_stats_counter_value(const gchar *key, StatsClusterLabel *labels, gsize labels_len)
{
  gsize value = 0;
  StatsClusterKey sc_key;
  stats_cluster_single_key_set(&sc_key, key, labels, labels_len);

  stats_lock();
  {
    StatsCounterItem *counter;
    StatsCluster *cluster = stats_register_dynamic_counter(0, &sc_key, SC_TYPE_SINGLE_VALUE, &counter);
    cr_assert(cluster, "Cluster does not exist, probably we have reached max-stats-dynamics().");

    value = stats_counter_get(counter);

    stats_unregister_dynamic_counter(cluster, SC_TYPE_SINGLE_VALUE, &counter);
  }
  stats_unlock();

  return value;
}

static inline void
metrics_probe_test_assert_counter_value(const gchar *key, StatsClusterLabel *labels, gsize labels_len,
                                        gsize expected_value)
{
  gsize actual_value = metrics_probe_test_get_stats_counter_value(key, labels, labels_len);

  GString *formatted_labels = NULL;
  if (expected_value != actual_value)
    {
      formatted_labels = g_string_new("(");

      for (gint i = 0; i < labels_len; i++)
        {
          if (i != 0)
            g_string_append(formatted_labels, ", ");

          g_string_append(formatted_labels, labels[i].name);
          g_string_append(formatted_labels, " => ");
          g_string_append(formatted_labels, labels[i].value);
        }

      g_string_append_c(formatted_labels, ')');
    }

  cr_assert(expected_value == actual_value,
            "Unexpected counter value. key: %s labels: %s expected: %lu actual: %lu",
            key, formatted_labels->str, expected_value, actual_value);

  if (formatted_labels)
    g_string_free(formatted_labels, TRUE);
}
