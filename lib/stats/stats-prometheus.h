/*
 * Copyright (c) 2023 László Várady
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
#ifndef STATS_PROMETHEUS_H_INCLUDED
#define STATS_PROMETHEUS_H_INCLUDED 1

#include "syslog-ng.h"
#include "stats-cluster.h"

#define PROMETHEUS_METRIC_PREFIX "syslogng_"

typedef void (*StatsPrometheusRecordFunc)(const char *record, gpointer user_data);

GString *stats_prometheus_format_counter(StatsCluster *sc, gint type, StatsCounterItem *counter);

void stats_generate_prometheus(StatsPrometheusRecordFunc process_record, gpointer user_data, gboolean with_legacy,
                               gboolean *cancelled);
void stats_prometheus_format_labels_append(StatsClusterLabel *labels, gsize labels_len, GString *buf);

#endif
