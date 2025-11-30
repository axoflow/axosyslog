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

#include "object-metrics-labels.h"
#include "filterx-mapping.h"
#include "object-string.h"
#include "object-primitive.h"
#include "object-extractor.h"
#include "filterx-eval.h"
#include "stats/stats-prometheus.h"
#include "stats/stats-cluster.h"
#include "scratch-buffers.h"

#include <string.h>

#define METRICS_LABELS_USAGE "Usage: metrics_labels() or metrics_labels({...}})"
#define DEDUP_METRICS_LABELS_USAGE "Usage: dedup_metrics_labels(my_metrics_labels)"

typedef struct _FilterXObjectMetricsLabels
{
  FilterXMapping super;
  GArray *labels;
  gboolean sorted;
  gboolean deduped;
} FilterXObjectMetricsLabels;

static gboolean
_truthy(FilterXObject *s)
{
  return TRUE;
}

static gboolean
_marshal(FilterXObject *s, GString *repr, LogMessageValueType *t)
{
  FilterXObjectMetricsLabels *self = (FilterXObjectMetricsLabels *) s;

  stats_prometheus_format_labels_append((StatsClusterLabel *) self->labels->data, self->labels->len, repr);
  if (t)
    *t = LM_VT_STRING;
  return TRUE;
}

static gboolean
_repr(FilterXObject *s, GString *repr)
{
  _marshal(s, repr, NULL);
  return TRUE;
}

static gboolean
_len(FilterXObject *s, guint64 *len)
{
  FilterXObjectMetricsLabels *self = (FilterXObjectMetricsLabels *) s;

  *len = self->labels->len;
  return TRUE;
}

static FilterXObject *
_get_subscript(FilterXObject *s, FilterXObject *key)
{
  FilterXObjectMetricsLabels *self = (FilterXObjectMetricsLabels *) s;

  const gchar *key_str;
  gsize key_len;
  const gchar *error;
  if (!filterx_mapping_normalize_key(key, &key_str, &key_len, &error))
    {
      filterx_eval_push_error(error, NULL, key);
      return NULL;
    }

  for (gssize i = (gssize)(self->labels->len) - 1; i >= 0; i--)
    {
      StatsClusterLabel *label = &g_array_index(self->labels, StatsClusterLabel, i);
      if (strncmp(label->name, key_str, key_len) == 0)
        return filterx_string_new(label->value, -1);
    }

  return NULL;
}

static const gchar *
_obj_to_string(FilterXObject *obj, gsize *len)
{
  const gchar *str;
  if (filterx_object_extract_string_ref(obj, &str, len))
    return str;

  GString *buf = scratch_buffers_alloc();
  if (filterx_object_str(obj, buf))
    {
      *len = buf->len;
      return buf->str;
    }

  return NULL;
}

static gboolean
_is_empty_value(FilterXObject *value)
{
  if (filterx_object_extract_null(value))
    return TRUE;

  gsize len;
  const gchar *str;
  if (!filterx_object_extract_string_ref(value, &str, &len))
    return FALSE;

  return len == 0;
}

static gboolean
_set_subscript(FilterXObject *s, FilterXObject *key, FilterXObject **new_value)
{
  FilterXObjectMetricsLabels *self = (FilterXObjectMetricsLabels *) s;

  const gchar *key_str;
  gsize key_len;
  const gchar *error;
  if (!filterx_mapping_normalize_key(key, &key_str, &key_len, &error))
    {
      filterx_eval_push_error(error, NULL, key);
      return FALSE;
    }

  if (_is_empty_value(*new_value))
    return TRUE;

  ScratchBuffersMarker marker;
  scratch_buffers_mark(&marker);

  gsize name_len, value_len;
  const gchar *name_str = _obj_to_string(key, &name_len);
  const gchar *value_str = _obj_to_string(*new_value, &value_len);
  if (!name_str || !value_str)
    return FALSE;

  StatsClusterLabel label = stats_cluster_label(g_strndup(name_str, name_len), g_strndup(value_str, value_len));
  g_array_append_val(self->labels, label);

  self->sorted = FALSE;
  self->deduped = FALSE;

  scratch_buffers_reclaim_marked(marker);
  return TRUE;
}

static gboolean
_is_key_set(FilterXObject *s, FilterXObject *key)
{
  FilterXObjectMetricsLabels *self = (FilterXObjectMetricsLabels *) s;

  const gchar *key_str;
  gsize key_len;
  const gchar *error;
  if (!filterx_mapping_normalize_key(key, &key_str, &key_len, &error))
    {
      filterx_eval_push_error(error, NULL, key);
      return FALSE;
    }

  for (gssize i = (gssize)(self->labels->len) - 1; i >= 0; i--)
    {
      StatsClusterLabel *label = &g_array_index(self->labels, StatsClusterLabel, i);
      if (strncmp(label->name, key_str, key_len) == 0)
        return TRUE;
    }

  return FALSE;
}

static gboolean
_unset_key(FilterXObject *s, FilterXObject *key)
{
  FilterXObjectMetricsLabels *self = (FilterXObjectMetricsLabels *) s;

  const gchar *key_str;
  gsize key_len;
  const gchar *error;
  if (!filterx_mapping_normalize_key(key, &key_str, &key_len, &error))
    {
      filterx_eval_push_error(error, NULL, key);
      return FALSE;
    }

  for (gssize i = (gssize)(self->labels->len) - 1; i >= 0; i--)
    {
      StatsClusterLabel *label = &g_array_index(self->labels, StatsClusterLabel, i);
      if (strncmp(label->name, key_str, key_len) == 0)
        g_array_remove_index(self->labels, i);
    }

  return TRUE;
}

static gboolean
_iter(FilterXObject *s, FilterXObjectIterFunc func, gpointer user_data)
{
  FilterXObjectMetricsLabels *self = (FilterXObjectMetricsLabels *) s;

  for (guint i = 0; i < self->labels->len; i++)
    {
      StatsClusterLabel *label = &g_array_index(self->labels, StatsClusterLabel, i);

      FILTERX_STRING_DECLARE_ON_STACK(name, label->name, -1);
      FILTERX_STRING_DECLARE_ON_STACK(value, label->value, -1);

      gboolean success = func(name, value, user_data);

      FILTERX_STRING_CLEAR_FROM_STACK(name);
      FILTERX_STRING_CLEAR_FROM_STACK(value);

      if (!success)
        return FALSE;
    }

  return TRUE;
}

static FilterXObject *
_clone(FilterXObject *s)
{
  FilterXObjectMetricsLabels *self = (FilterXObjectMetricsLabels *) s;

  FilterXObjectMetricsLabels *cloned = \
                                       (FilterXObjectMetricsLabels *) filterx_object_metrics_labels_new(self->labels->len);

  for (guint i = 0; i < self->labels->len; i++)
    {
      StatsClusterLabel *label = &g_array_index(self->labels, StatsClusterLabel, i);
      StatsClusterLabel cloned_label = stats_cluster_label(g_strdup(label->name), g_strdup(label->value));
      g_array_append_val(cloned->labels, cloned_label);
    }

  return &cloned->super.super;
}

static void
_label_destroy(StatsClusterLabel *label)
{
  g_free((gchar *) label->name);
  g_free((gchar *) label->value);
}

static void
_free(FilterXObject *s)
{
  FilterXObjectMetricsLabels *self = (FilterXObjectMetricsLabels *) s;

  for (guint i = 0; i < self->labels->len; i++)
    _label_destroy(&g_array_index(self->labels, StatsClusterLabel, i));

  g_array_free(self->labels, TRUE);
  filterx_object_free_method(s);
}

static void
_dedup(FilterXObjectMetricsLabels *self)
{
  GHashTable *labels_map = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, (GDestroyNotify) _label_destroy);

  for (guint i = 0; i < self->labels->len; i++)
    {
      StatsClusterLabel *label = &g_array_index(self->labels, StatsClusterLabel, i);
      g_hash_table_replace(labels_map, (gpointer) label->name, label);
    }

  GArray *new_labels = g_array_sized_new(FALSE, FALSE, sizeof(StatsClusterLabel), self->labels->len);

  GHashTableIter iter;
  g_hash_table_iter_init(&iter, labels_map);

  gpointer k, v;
  while (g_hash_table_iter_next(&iter, &k, &v))
    {
      StatsClusterLabel *label = v;
      g_array_append_val(new_labels, *label);
    }

  g_array_free(self->labels, TRUE);
  self->labels = new_labels;
  self->deduped = TRUE;

  g_hash_table_steal_all(labels_map);
  g_hash_table_unref(labels_map);
}

static gint
_label_cmp(const StatsClusterLabel *lhs, const StatsClusterLabel *rhs)
{
  return strcmp(lhs->name, rhs->name);
}

StatsClusterLabel *
filterx_object_metrics_labels_get_value_ref(FilterXObject *s, gsize *len)
{
  FilterXObjectMetricsLabels *self = (FilterXObjectMetricsLabels *) s;

  if (!self->sorted)
    {
      g_array_sort(self->labels, (GCompareFunc) _label_cmp);
      self->sorted = TRUE;
    }

  *len = self->labels->len;
  return (StatsClusterLabel *) self->labels->data;
}

FilterXObject *
filterx_object_metrics_labels_new(guint reserved_size)
{
  FilterXObjectMetricsLabels *self = g_new0(FilterXObjectMetricsLabels, 1);
  filterx_mapping_init_instance(&self->super, &FILTERX_TYPE_NAME(metrics_labels));


  self->labels = g_array_sized_new(FALSE, FALSE, sizeof(StatsClusterLabel), reserved_size);

  return &self->super.super;
}

FilterXObject *
filterx_simple_function_metrics_labels(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  if (!args || !args_len)
    return filterx_object_metrics_labels_new(16);

  if (args_len != 1)
    {
      filterx_simple_function_argument_error(s, "unexpected number of arguments. "
                                             METRICS_LABELS_USAGE);
      return NULL;
    }

  FilterXObject *obj = args[0];
  FilterXObject *typed_obj = filterx_ref_unwrap_ro(obj);

  if (filterx_object_is_type(typed_obj, &FILTERX_TYPE_NAME(metrics_labels)))
    return filterx_object_ref(obj);

  if (!filterx_object_is_type(typed_obj, &FILTERX_TYPE_NAME(mapping)))
    {
      filterx_simple_function_argument_error(s, "unexpected type of argument. "
                                             METRICS_LABELS_USAGE);
      return NULL;
    }

  guint64 len;
  g_assert(filterx_object_len(typed_obj, &len));
  FilterXObject *metrics_labels = filterx_object_metrics_labels_new(MIN(len, (guint64) G_MAXUINT));

  if (!filterx_mapping_merge(metrics_labels, typed_obj))
    {
      filterx_object_unref(metrics_labels);
      filterx_simple_function_argument_error(s, "failed to cast dict into metrics_labels. "
                                             METRICS_LABELS_USAGE);
      return NULL;
    }

  return metrics_labels;
}

static FilterXObject *
_dedup_extract_obj_arg(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  if (!args || args_len != 1)
    {
      filterx_simple_function_argument_error(s, "unexpected number of arguments. "
                                             DEDUP_METRICS_LABELS_USAGE);
      return NULL;
    }

  FilterXObject *obj = args[0];
  FilterXObject *typed_obj = filterx_ref_unwrap_ro(obj);
  if (!filterx_object_is_type(typed_obj, &FILTERX_TYPE_NAME(metrics_labels)))
    {
      filterx_simple_function_argument_error(s, "unexpected argument type. "
                                             DEDUP_METRICS_LABELS_USAGE);
      return NULL;
    }

  return obj;
}

FilterXObject *
filterx_simple_function_dedup_metrics_labels(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  FilterXObject *obj = _dedup_extract_obj_arg(s, args, args_len);
  if (!obj)
    return NULL;

  FilterXObjectMetricsLabels *typed_obj = (FilterXObjectMetricsLabels *) filterx_ref_unwrap_ro(obj);
  if (typed_obj->deduped)
    return filterx_boolean_new(TRUE);

  typed_obj = (FilterXObjectMetricsLabels *) filterx_ref_unwrap_rw(obj);
  _dedup(typed_obj);
  return filterx_boolean_new(TRUE);
}

FILTERX_DEFINE_TYPE(metrics_labels, FILTERX_TYPE_NAME(mapping),
                    .is_mutable = TRUE,
                    .truthy = _truthy,
                    .marshal = _marshal,
                    .repr = _repr,
                    .clone = _clone,
                    .get_subscript = _get_subscript,
                    .set_subscript = _set_subscript,
                    .is_key_set = _is_key_set,
                    .unset_key = _unset_key,
                    .iter = _iter,
                    .len = _len,
                    .free_fn = _free,
                   );
