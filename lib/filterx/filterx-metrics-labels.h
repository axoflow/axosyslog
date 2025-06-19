/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 Attila Szakacs <attila.szakacs@axoflow.com>
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

#ifndef FILTERX_METRICS_LABELS_H_INCLUDED
#define FILTERX_METRICS_LABELS_H_INCLUDED

#include "filterx-expr.h"
#include "metrics/dyn-metrics-store.h"

typedef struct _FilterXMetricsLabels FilterXMetricsLabels;

FilterXMetricsLabels *filterx_metrics_labels_new(FilterXExpr *labels);
void filterx_metrics_labels_optimize(FilterXMetricsLabels *self);
gboolean filterx_metrics_labels_init(FilterXMetricsLabels *self, GlobalConfig *cfg);
void filterx_metrics_labels_deinit(FilterXMetricsLabels *self, GlobalConfig *cfg);
void filterx_metrics_labels_free(FilterXMetricsLabels *self);

gboolean filterx_metrics_labels_format(FilterXMetricsLabels *self, DynMetricsStore *store,
                                       StatsClusterLabel **labels, gsize *len);
gboolean filterx_metrics_labels_is_const(FilterXMetricsLabels *self);

#endif
