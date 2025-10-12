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

#ifndef STATS_CLUSTER_HIST_H_INCLUDED
#define STATS_CLUSTER_HIST_H_INCLUDED

#include "syslog-ng.h"

typedef enum
{
  SC_TYPE_HIST_SUM=0,     /* the sum of all observations */
  SC_TYPE_HIST_COUNT,     /* number of observations */
  SC_TYPE_HIST_BUCKET0,
  SC_TYPE_HIST_BUCKET1,
  SC_TYPE_HIST_BUCKET2,
  SC_TYPE_HIST_BUCKET3,
  SC_TYPE_HIST_BUCKET4,
  SC_TYPE_HIST_BUCKET5,
  SC_TYPE_HIST_BUCKET6,
  SC_TYPE_HIST_BUCKET7,
  SC_TYPE_HIST_BUCKET8,
  SC_TYPE_HIST_BUCKET9,
  SC_TYPE_HIST_BUCKET10,
  SC_TYPE_HIST_BUCKET11,
  SC_TYPE_HIST_BUCKET12,
  SC_TYPE_HIST_BUCKET13,
  SC_TYPE_HIST_BUCKET14,
  SC_TYPE_HIST_BUCKET15,
  SC_TYPE_HIST_MAX,
  SC_TYPE_HIST_MAX_BUCKETS=SC_TYPE_HIST_MAX - SC_TYPE_HIST_BUCKET0,
} StatsCounterGroupHist;


void stats_cluster_hist_key_set(StatsClusterKey *key, const gchar *name,
                                StatsClusterLabel *labels, gsize labels_len);

void stats_cluster_hist_key_add_legacy_alias(StatsClusterKey *key,
                                             guint16 component, const gchar *id, const gchar *instance);

#endif
