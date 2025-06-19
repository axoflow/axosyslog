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

#ifndef FILTERX_OBJECT_METRICS_LABELS_H_INCLUDED
#define FILTERX_OBJECT_METRICS_LABELS_H_INCLUDED

#include "filterx-object.h"
#include "filterx/expr-function.h"

FILTERX_DECLARE_TYPE(metrics_labels);

FilterXObject *filterx_object_metrics_labels_new(guint reserved_size);
StatsClusterLabel *filterx_object_metrics_labels_get_value_ref(FilterXObject *s, gsize *len);
FilterXObject *filterx_simple_function_metrics_labels(FilterXExpr *s, FilterXObject *args[], gsize args_len);
FilterXObject *filterx_simple_function_dedup_metrics_labels(FilterXExpr *s, FilterXObject *args[], gsize args_len);

#endif
