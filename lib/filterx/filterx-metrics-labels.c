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

#include "filterx-metrics-labels.h"
#include "filterx-eval.h"
#include "expr-literal.h"
#include "expr-literal-container.h"
#include "object-string.h"
#include "object-null.h"
#include "object-extractor.h"
#include "filterx-mapping.h"
#include "object-metrics-labels.h"
#include "metrics/dyn-metrics-cache.h"
#include "stats/stats-cluster-single.h"
#include "scratch-buffers.h"

#include <string.h>

typedef struct _FilterXMetricsLabel
{
  gchar *name;
  struct
  {
    FilterXExpr *expr;
    gchar *str;
  } value;
} FilterXMetricsLabel;

gboolean
_is_value_empty(FilterXObject *value)
{
  if (filterx_object_extract_null(value))
    return TRUE;

  gsize len;
  const gchar *str;
  if (filterx_object_extract_string_ref(value, &str, &len) && len == 0)
    return TRUE;

  return FALSE;
}

static gchar *
_format_str_obj(FilterXObject *obj)
{
  GString *str = scratch_buffers_alloc();
  return filterx_object_str(obj, str) ? str->str : NULL;
}

static gboolean
_format_value(FilterXObject *obj, gchar **formatted_value)
{
  if (_is_value_empty(obj))
    {
      /* empty values should not be added, but it is not a failure */
      return TRUE;
    }

  *formatted_value = _format_str_obj(obj);
  return (*formatted_value) != NULL;
}

static gboolean
_format_value_expr(FilterXExpr *expr, gchar **formatted_value)
{
  gboolean success = TRUE;

  FilterXObject *obj = filterx_expr_eval_typed(expr);
  if (!obj)
    {
      success = FALSE;
      filterx_eval_push_error_static_info("Failed to format metrics label value", expr,
                                          "Failed to evaluate expression");
      goto exit;
    }
  success = _format_value(obj, formatted_value);
  if (!success)
    {
      filterx_eval_push_error("Failed to format metrics label value: str() failed", expr, obj);
    }
exit:
  filterx_object_unref(obj);
  return success;
}

static gboolean
_label_format(FilterXMetricsLabel *self, DynMetricsStore *store)
{
  gchar *value = NULL;

  if (self->value.str)
    {
      value = self->value.str;
    }
  else if (!_format_value_expr(self->value.expr, &value))
    {
      return FALSE;
    }

  if (!value)
    {
      /* empty values should not be added, but it is not a failure */
      return TRUE;
    }

  StatsClusterLabel *sc_label = dyn_metrics_store_cache_label(store);
  sc_label->name = self->name;
  sc_label->value = value;

  return TRUE;
}

static gboolean
_label_is_const(FilterXMetricsLabel *self)
{
  return !!self->value.str;
}

static gint
_label_cmp(gconstpointer a, gconstpointer b)
{
  const FilterXMetricsLabel *lhs = *(const FilterXMetricsLabel **) a;
  const FilterXMetricsLabel *rhs = *(const FilterXMetricsLabel **) b;

  return strcmp(lhs->name, rhs->name);
}

static void
_label_optimize(FilterXMetricsLabel *self, gboolean *is_empty)
{
  self->value.expr = filterx_expr_optimize(self->value.expr);

  if (!filterx_expr_is_literal(self->value.expr))
    return;

  ScratchBuffersMarker marker;
  scratch_buffers_mark(&marker);

  gchar *formatted_value = NULL;

  FilterXObject *value = filterx_literal_get_value(self->value.expr);
  g_assert(_format_value(value, &formatted_value));
  filterx_object_unref(value);

  self->value.str = g_strdup(formatted_value);
  scratch_buffers_reclaim_marked(marker);

  filterx_expr_unref(self->value.expr);
  self->value.expr = NULL;

  *is_empty = !self->value.str;
}

static gboolean
_label_init(FilterXMetricsLabel *self, GlobalConfig *cfg)
{
  return filterx_expr_init(self->value.expr, cfg);
}

static void
_label_deinit(FilterXMetricsLabel *self, GlobalConfig *cfg)
{
  filterx_expr_deinit(self->value.expr, cfg);
}

static void
_label_free(FilterXMetricsLabel *self)
{
  g_free(self->name);
  filterx_expr_unref(self->value.expr);
  g_free(self->value.str);

  g_free(self);
}

static gchar *
_init_label_name(FilterXExpr *name)
{
  if (!filterx_expr_is_literal(name))
    {
      filterx_eval_push_error_static_info("Failed to initialize metrics label name", name,
                                          "Name must be a string literal, got an expression");
      return NULL;
    }

  FilterXObject *obj = filterx_literal_get_value(name);
  gchar *str = g_strdup(filterx_string_get_value_ref(obj, NULL));
  if (!str)
    {
      filterx_eval_push_error_info_printf("Failed to initialize metrics label name", name,
                                          "Name must be a string literal, got: %s",
                                          filterx_object_get_type_name(obj));
    }

  filterx_object_unref(obj);
  return str;
}

static FilterXMetricsLabel *
_label_new(FilterXExpr *name, FilterXExpr *value)
{
  FilterXMetricsLabel *self = g_new0(FilterXMetricsLabel, 1);

  self->name = _init_label_name(name);
  if (!self->name)
    goto error;

  self->value.expr = filterx_expr_ref(value);

  return self;

error:
  _label_free(self);
  return NULL;
}


struct _FilterXMetricsLabels
{
  FilterXExpr *expr;
  GPtrArray *literal_labels;
  gboolean is_const;
};

static void
_check_label_is_const(gpointer data, gpointer user_data)
{
  FilterXMetricsLabel *label = (FilterXMetricsLabel *) data;
  gboolean *is_const = (gboolean *) user_data;

  if (!(*is_const))
    return;

  if (!_label_is_const(label))
    *is_const = FALSE;
}

gboolean
filterx_metrics_labels_is_const(FilterXMetricsLabels *self)
{
  return self->is_const;
}

static gboolean
_format_dict_elem_to_store(FilterXObject *key, FilterXObject *value, gpointer user_data)
{
  DynMetricsStore *store = (DynMetricsStore *) user_data;

  const gchar *name_str = _format_str_obj(key);
  if (!name_str)
    {
      filterx_eval_push_error_info_printf("Failed to format label name", NULL,
                                          "Name must be string, got: %s",
                                          filterx_object_get_type_name(key));
      return FALSE;
    }

  if (_is_value_empty(value))
    {
      /* empty values should not be added, but it is not a failure */
      return TRUE;
    }

  const gchar *value_str = _format_str_obj(value);
  if (!value_str)
    {
      filterx_eval_push_error_static_info("Failed to format label value", NULL,
                                          "Object does not support string casting");
      return FALSE;
    }

  StatsClusterLabel *label = dyn_metrics_store_cache_label(store);
  label->name = name_str;
  label->value = value_str;

  return TRUE;
}

static gboolean
_format_dict_to_store(FilterXObject *obj, DynMetricsStore *store, StatsClusterLabel **labels, gsize *len)
{
  FilterXObject *typed_obj = filterx_ref_unwrap_ro(obj);
  if (!filterx_object_is_type(typed_obj, &FILTERX_TYPE_NAME(mapping)))
    {
      filterx_eval_push_error_info_printf("Failed to format metrics labels", NULL,
                                          "Labels must be a dict, got: %s",
                                          filterx_object_get_type_name(obj));
      return FALSE;
    }

  if (!filterx_object_iter(typed_obj, _format_dict_elem_to_store, store))
    return FALSE;

  dyn_metrics_store_sort_cached_labels(store);

  *labels = dyn_metrics_store_get_cached_labels(store);
  *len = dyn_metrics_store_get_cached_labels_len(store);

  return TRUE;
}

static gboolean
_format_expr(FilterXExpr *expr, DynMetricsStore *store, StatsClusterLabel **labels, gsize *len)
{
  FilterXObject *obj = filterx_expr_eval_typed(expr);
  if (!obj)
    {
      filterx_eval_push_error_static_info("Failed to format metrics labels", expr,
                                          "Failed to evaluate expression");
      return FALSE;
    }

  gboolean success = TRUE;

  FilterXObject *typed_obj = filterx_ref_unwrap_ro(obj);
  if (filterx_object_is_type(typed_obj, &FILTERX_TYPE_NAME(metrics_labels)))
    {
      *labels = filterx_object_metrics_labels_get_value_ref(typed_obj, len);
      g_assert(labels);
      goto exit;
    }

  success = _format_dict_to_store(obj, store, labels, len);

exit:
  filterx_object_unref(obj);
  return success;
}

static void
_format_label_to_store(gpointer data, gpointer user_data)
{
  FilterXMetricsLabel *label = (FilterXMetricsLabel *) data;
  gboolean *success = ((gpointer *) user_data)[0];
  DynMetricsStore *store = ((gpointer *) user_data)[1];

  if (!(*success))
    return;

  *success = _label_format(label, store);
}

/* NOTE: Names and values of the labels are stored in ScratchBuffers. */
gboolean
filterx_metrics_labels_format(FilterXMetricsLabels *self, DynMetricsStore *store,
                              StatsClusterLabel **labels, gsize *len)
{
  dyn_metrics_store_reset_labels_cache(store);

  gboolean success;
  if (self->expr)
    {
      success = _format_expr(self->expr, store, labels, len);
    }
  else
    {
      success = TRUE;
      gpointer user_data[] = { &success, store };
      g_ptr_array_foreach(self->literal_labels, _format_label_to_store, user_data);
      *labels = dyn_metrics_store_get_cached_labels(store);
      *len = dyn_metrics_store_get_cached_labels_len(store);
    }

  if (!success)
    return FALSE;

  return TRUE;
}

static gboolean
_calculate_constness(FilterXMetricsLabels *self)
{
  if (self->expr)
    return FALSE;

  gboolean is_const = TRUE;
  g_ptr_array_foreach(self->literal_labels, _check_label_is_const, &is_const);
  return is_const;
}

void
filterx_metrics_labels_optimize(FilterXMetricsLabels *self)
{
  self->expr = filterx_expr_optimize(self->expr);

  if (self->literal_labels)
    {
      for (guint i = 0; i < self->literal_labels->len; i++)
        {
          FilterXMetricsLabel *label = g_ptr_array_index(self->literal_labels, i);
          gboolean is_empty = FALSE;

          _label_optimize(label, &is_empty);

          if (is_empty)
            {
              g_ptr_array_remove_index(self->literal_labels, i);
              i--;
            }
        }
    }

  self->is_const = _calculate_constness(self);
}

gboolean
filterx_metrics_labels_init(FilterXMetricsLabels *self, GlobalConfig *cfg)
{
  if (!filterx_expr_init(self->expr, cfg))
    return FALSE;

  if (self->literal_labels)
    {
      for (guint i = 0; i < self->literal_labels->len; i++)
        {
          FilterXMetricsLabel *label = g_ptr_array_index(self->literal_labels, i);
          if (!_label_init(label, cfg))
            {
              for (guint j = 0; j < i; j++)
                {
                  label = g_ptr_array_index(self->literal_labels, j);
                  _label_deinit(label, cfg);
                }
              filterx_expr_deinit(self->expr, cfg);
              return FALSE;
            }
        }
    }

  return TRUE;
}

void
filterx_metrics_labels_deinit(FilterXMetricsLabels *self, GlobalConfig *cfg)
{
  filterx_expr_deinit(self->expr, cfg);

  if (self->literal_labels)
    {
      for (guint i = 0; i < self->literal_labels->len; i++)
        {
          FilterXMetricsLabel *label = g_ptr_array_index(self->literal_labels, i);
          _label_deinit(label, cfg);
        }
    }
}

void
filterx_metrics_labels_free(FilterXMetricsLabels *self)
{
  filterx_expr_unref(self->expr);
  if (self->literal_labels)
    g_ptr_array_unref(self->literal_labels);

  g_free(self);
}

static gboolean
_add_literal_label(FilterXExpr *key, FilterXExpr *value, gpointer user_data)
{
  GPtrArray *literal_labels = (GPtrArray *) user_data;

  FilterXMetricsLabel *label = _label_new(key, value);
  if (!label)
    return FALSE;

  g_ptr_array_add(literal_labels, label);
  return TRUE;
}

static GPtrArray *
_init_literal_labels(FilterXExpr *labels_expr)
{
  GPtrArray *literal_labels = g_ptr_array_new_with_free_func((GDestroyNotify) _label_free);

  if (!filterx_literal_dict_foreach(labels_expr, _add_literal_label, literal_labels))
    {
      g_ptr_array_unref(literal_labels);
      literal_labels = NULL;
      goto exit;
    }

  g_ptr_array_sort(literal_labels, _label_cmp);

exit:
  return literal_labels;
}

static gboolean
_init_labels(FilterXMetricsLabels *self, FilterXExpr *labels)
{
  if (!labels)
    {
      self->literal_labels = g_ptr_array_new_with_free_func((GDestroyNotify) _label_free);
      return TRUE;
    }

  if (filterx_expr_is_literal_dict(labels))
    {
      self->literal_labels = _init_literal_labels(labels);
      return !!self->literal_labels;
    }

  self->expr = filterx_expr_ref(labels);
  return TRUE;
}

FilterXMetricsLabels *
filterx_metrics_labels_new(FilterXExpr *labels)
{
  FilterXMetricsLabels *self = g_new0(FilterXMetricsLabels, 1);

  if (!_init_labels(self, labels))
    {
      filterx_metrics_labels_free(self);
      return NULL;
    }

  self->is_const = FALSE;

  return self;
}
