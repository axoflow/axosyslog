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

#include "filterx-metrics.h"
#include "filterx-metrics-labels.h"
#include "expr-literal.h"
#include "object-extractor.h"
#include "object-string.h"
#include "filterx-eval.h"
#include "stats/stats-cluster-single.h"
#include "stats/stats-registry.h"
#include "metrics/dyn-metrics-cache.h"
#include "scratch-buffers.h"

struct _FilterXMetrics
{
  struct
  {
    FilterXExpr *expr;
    gchar *str;
  } key;

  FilterXMetricsLabels *labels;
  gint level;

  StatsCluster *const_cluster;
};

gboolean
filterx_metrics_is_enabled(FilterXMetrics *self)
{
  return stats_check_level(self->level);
}

static const gchar *
_format_sck_name(FilterXMetrics *self)
{
  if (self->key.str)
    return self->key.str;

  FilterXObject *key_obj = filterx_expr_eval(self->key.expr);
  if (!key_obj)
    return NULL;

  gsize len;
  const gchar *name;
  if (!filterx_object_extract_string_ref(key_obj, &name, &len) || len == 0)
    {
      filterx_eval_push_error("failed to format metrics key: key must be a non-empty string", self->key.expr, key_obj);
      goto exit;
    }

  GString *name_buffer = scratch_buffers_alloc();
  g_string_append_len(name_buffer, name, (gssize) len);
  name = name_buffer->str;

exit:
  filterx_object_unref(key_obj);
  return name;
}

static gboolean
_format_sck(FilterXMetrics *self, StatsClusterKey *sck, DynMetricsStore *store)
{
  const gchar *name = _format_sck_name(self);
  if (!name)
    return FALSE;

  StatsClusterLabel *labels;
  gsize labels_len;
  if (!filterx_metrics_labels_format(self->labels, store, &labels, &labels_len))
    return FALSE;

  stats_cluster_single_key_set(sck, name, labels, labels_len);
  return TRUE;
}

static gboolean
_is_const(FilterXMetrics *self)
{
  return !self->key.expr && (self->const_cluster || filterx_metrics_labels_is_const(self->labels));
}

static void
_optimize_key(FilterXMetrics *self)
{
  self->key.expr = filterx_expr_optimize(self->key.expr);

  if (!filterx_expr_is_literal(self->key.expr))
    return;

  FilterXObject *key_obj = filterx_expr_eval_typed(self->key.expr);
  if (!filterx_object_is_type(key_obj, &FILTERX_TYPE_NAME(string)))
    {
      filterx_object_unref(key_obj);
      return;
    }

  /* There are no literal message values, so we don't need to call extract_string() here. */
  self->key.str = g_strdup(filterx_string_get_value_ref(key_obj, NULL));

  filterx_expr_unref(self->key.expr);
  self->key.expr = NULL;

  filterx_object_unref(key_obj);
}

static void
_optimize_counter(FilterXMetrics *self)
{
  DynMetricsStore *store = dyn_metrics_cache();

  stats_lock();

  if (!self->key.str || !filterx_metrics_labels_is_const(self->labels))
    goto exit;

  ScratchBuffersMarker marker;
  scratch_buffers_mark(&marker);

  StatsClusterKey sck;
  if (!_format_sck(self, &sck, store))
    {
      msg_debug("FilterX: Failed to optimize metrics, continuing unoptimized");
      scratch_buffers_reclaim_marked(marker);
      goto exit;
    }

  StatsCounterItem *counter;
  self->const_cluster = stats_register_dynamic_counter(self->level, &sck, SC_TYPE_SINGLE_VALUE, &counter);

  scratch_buffers_reclaim_marked(marker);

  g_free(self->key.str);
  self->key.str = NULL;

exit:
  stats_unlock();
}

void
filterx_metrics_optimize(FilterXMetrics *self)
{
  _optimize_key(self);
  filterx_metrics_labels_optimize(self->labels);
  _optimize_counter(self);
}

gboolean
filterx_metrics_get_stats_counter(FilterXMetrics *self, StatsCounterItem **counter)
{
  DynMetricsStore *store = dyn_metrics_cache();

  if (!filterx_metrics_is_enabled(self))
    {
      *counter = NULL;
      return TRUE;
    }

  if (_is_const(self))
    {
      *counter = stats_cluster_single_get_counter(self->const_cluster);
      return TRUE;
    }

  gboolean success = FALSE;

  ScratchBuffersMarker marker;
  scratch_buffers_mark(&marker);

  StatsClusterKey sck;
  if (!_format_sck(self, &sck, store))
    goto exit;

  *counter = dyn_metrics_store_retrieve_counter(store, &sck, self->level);
  success = TRUE;

exit:
  scratch_buffers_reclaim_marked(marker);
  return success;
}

gboolean
filterx_metrics_init(FilterXMetrics *self, GlobalConfig *cfg)
{
  if (!filterx_expr_init(self->key.expr, cfg))
    return FALSE;

  if (!filterx_metrics_labels_init(self->labels, cfg))
    {
      filterx_expr_deinit(self->key.expr, cfg);
      return FALSE;
    }

  return TRUE;
}

void
filterx_metrics_deinit(FilterXMetrics *self, GlobalConfig *cfg)
{
  filterx_expr_deinit(self->key.expr, cfg);
  filterx_metrics_labels_deinit(self->labels, cfg);
}

void
filterx_metrics_free(FilterXMetrics *self)
{
  filterx_expr_unref(self->key.expr);
  g_free(self->key.str);

  if (self->labels)
    filterx_metrics_labels_free(self->labels);

  stats_lock();
  {
    StatsCounterItem *counter = stats_cluster_single_get_counter(self->const_cluster);
    stats_unregister_dynamic_counter(self->const_cluster, SC_TYPE_SINGLE_VALUE, &counter);
  }
  stats_unlock();

  g_free(self);
}

FilterXMetrics *
filterx_metrics_new(gint level, FilterXExpr *key, FilterXExpr *labels)
{
  FilterXMetrics *self = g_new0(FilterXMetrics, 1);

  g_assert(key);

  self->level = level;
  self->key.expr = filterx_expr_ref(key);

  self->labels = filterx_metrics_labels_new(labels);
  if (!self->labels)
    goto error;

  return self;

error:
  filterx_metrics_free(self);
  return NULL;
}
