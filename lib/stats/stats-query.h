/*
 * Copyright (c) 2017 Balabit
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

#ifndef STATS_QUERY_H_INCLUDED
#define STATS_QUERY_H_INCLUDED

#include "stats-cluster.h"
#include "syslog-ng.h"

typedef gboolean (*StatsFormatCb)(gpointer user_data);

gboolean stats_query_list(const gchar *expr, StatsFormatCb format_cb, gpointer result);
gboolean stats_query_list_and_reset_counters(const gchar *expr, StatsFormatCb format_cb, gpointer result);
gboolean stats_query_get(const gchar *expr, StatsFormatCb format_cb, gpointer result);
gboolean stats_query_get_and_reset_counters(const gchar *expr, StatsFormatCb format_cb, gpointer result);
gboolean stats_query_get_sum(const gchar *expr, StatsFormatCb format_cb, gpointer result);
gboolean stats_query_get_sum_and_reset_counters(const gchar *expr, StatsFormatCb format_cb, gpointer result);

#endif
