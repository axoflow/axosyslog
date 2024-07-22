/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 Attila Szakacs <attila.szakacs@axoflow.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#ifndef FILTERX_METRICS_H_INCLUDED
#define FILTERX_METRICS_H_INCLUDED

#include "filterx-expr.h"
#include "stats/stats-counter.h"

typedef struct _FilterXMetrics FilterXMetrics;

gboolean filterx_metrics_is_enabled(FilterXMetrics *self);
gboolean filterx_metrics_get_stats_counter(FilterXMetrics *self, StatsCounterItem **counter);

FilterXMetrics *filterx_metrics_new(gint level, FilterXExpr *key, FilterXExpr *labels);
void filterx_metrics_free(FilterXMetrics *self);

#endif
