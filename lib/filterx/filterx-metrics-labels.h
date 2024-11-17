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

#ifndef FILTERX_METRICS_LABELS_H_INCLUDED
#define FILTERX_METRICS_LABELS_H_INCLUDED

#include "filterx-expr.h"
#include "metrics/dyn-metrics-store.h"

typedef struct _FilterXMetricsLabels FilterXMetricsLabels;

FilterXMetricsLabels *filterx_metrics_labels_new(FilterXExpr *labels);
gboolean filterx_metrics_labels_init(FilterXMetricsLabels *self, GlobalConfig *cfg);
void filterx_metrics_labels_deinit(FilterXMetricsLabels *self, GlobalConfig *cfg);
void filterx_metrics_labels_free(FilterXMetricsLabels *self);

gboolean filterx_metrics_labels_format(FilterXMetricsLabels *self, DynMetricsStore *store,
                                       StatsClusterLabel **labels, gsize *len);
gboolean filterx_metrics_labels_is_const(FilterXMetricsLabels *self);

#endif
