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
#include "object-dict-interface.h"
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
  FilterXDict super;
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

static guint64
_len(FilterXDict *s)
{
  FilterXObjectMetricsLabels *self = (FilterXObjectMetricsLabels *) s;

  return self->labels->len;
}

static FilterXObject *
_get_subscript(FilterXDict *s, FilterXObject *key)
{
  FilterXObjectMetricsLabels *self = (FilterXObjectMetricsLabels *) s;

  const gchar *key_str;
  if (!filterx_object_extract_string_ref(key, &key_str, NULL))
    return NULL;

  for (gssize i = (gssize)(self->labels->len) - 1; i >= 0; i--)
    {
      StatsClusterLabel *label = &g_array_index(self->labels, StatsClusterLabel, i);
      if (strcmp(label->name, key_str) == 0)
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
_set_subscript(FilterXDict *s, FilterXObject *key, FilterXObject **new_value)
{
  FilterXObjectMetricsLabels *self = (FilterXObjectMetricsLabels *) s;

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

  if (!filterx_object_is_type(*new_value, &FILTERX_TYPE_NAME(string)))
    {
      filterx_object_unref(*new_value);
      *new_value = filterx_string_new(value_str, value_len);
    }

  scratch_buffers_reclaim_marked(marker);
  return TRUE;
}

static gboolean
_unset_key(FilterXDict *s, FilterXObject *key)
{
  FilterXObjectMetricsLabels *self = (FilterXObjectMetricsLabels *) s;

  const gchar *key_str;
  if (!filterx_object_extract_string_ref(key, &key_str, NULL))
    return FALSE;

  for (gssize i = (gssize)(self->labels->len) - 1; i >= 0; i--)
    {
      StatsClusterLabel *label = &g_array_index(self->labels, StatsClusterLabel, i);
      if (strcmp(label->name, key_str) == 0)
        g_array_remove_index(self->labels, i);
    }

  return TRUE;
}

static gboolean
_iter(FilterXDict *s, FilterXDictIterFunc func, gpointer user_data)
{
  FilterXObjectMetricsLabels *self = (FilterXObjectMetricsLabels *) s;

  for (guint i = 0; i < self->labels->len; i++)
    {
      StatsClusterLabel *label = &g_array_index(self->labels, StatsClusterLabel, i);

      FILTERX_STRING_DECLARE_ON_STACK(name, label->name, -1);
      FILTERX_STRING_DECLARE_ON_STACK(value, label->value, -1);

      gboolean success = func(name, value, user_data);

      filterx_object_unref(name);
      filterx_object_unref(value);

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

static gint
_gssize_cmp_reverse(const void *a, const void *b)
{
  gssize va = *(const gssize *)a;
  gssize vb = *(const gssize *)b;
  return vb - va;
}

static void
_dedup(FilterXObject *s)
{
  FilterXObjectMetricsLabels *typed_self = (FilterXObjectMetricsLabels *) filterx_ref_unwrap_rw(s);

  GArray *label_indexes_to_remove = g_array_sized_new(FALSE, FALSE, sizeof(gssize), typed_self->labels->len);
  GHashTable *labels_indexes_map = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, NULL);

  for (gssize i = ((gssize) (typed_self->labels->len)) - 1; i >= 0; i--)
    {
      StatsClusterLabel *label = &g_array_index(typed_self->labels, StatsClusterLabel, i);

      if (g_hash_table_contains(labels_indexes_map, label->name))
        {
          label_indexes_to_remove = g_array_append_val(label_indexes_to_remove, i);
          continue;
        }

      g_hash_table_insert(labels_indexes_map, (gpointer) label->name, NULL);
    }

  if (!label_indexes_to_remove->len)
    goto exit;

  g_array_sort(label_indexes_to_remove, _gssize_cmp_reverse);

  for (guint i = 0; i < label_indexes_to_remove->len; i++)
    {
      gssize index = g_array_index(label_indexes_to_remove, gssize, i);
      StatsClusterLabel *label = &g_array_index(typed_self->labels, StatsClusterLabel, index);
      _label_destroy(label);
      g_array_remove_index_fast(typed_self->labels, index);
    }

exit:
  typed_self->deduped = TRUE;

  g_hash_table_unref(labels_indexes_map);
  g_array_free(label_indexes_to_remove, TRUE);
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

  if (!self->deduped)
    _dedup(s);

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
  filterx_dict_init_instance(&self->super, &FILTERX_TYPE_NAME(metrics_labels));

  self->super.get_subscript = _get_subscript;
  self->super.set_subscript = _set_subscript;
  self->super.unset_key = _unset_key;
  self->super.len = _len;
  self->super.iter = _iter;

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

  if (!filterx_object_is_type(typed_obj, &FILTERX_TYPE_NAME(dict)))
    {
      filterx_simple_function_argument_error(s, "unexpected type of argument. "
                                             METRICS_LABELS_USAGE);
      return NULL;
    }

  guint64 len;
  g_assert(filterx_object_len(typed_obj, &len));
  FilterXObject *metrics_labels = filterx_object_metrics_labels_new(MIN(len, (guint64) G_MAXUINT));

  if (!filterx_dict_merge(metrics_labels, typed_obj))
    {
      filterx_object_unref(metrics_labels);
      filterx_simple_function_argument_error(s, "failed to cast dict into metrics_labels. "
                                             METRICS_LABELS_USAGE);
      return NULL;
    }

  return metrics_labels;
}

FilterXObject *
filterx_simple_function_dedup_metrics_labels(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  msg_warning_once("The dedup_metrics_labels() function is deprecated, deduplication happens automatically. "
                   "Please remove calls to dedup_metrics_labels() from your configuration as it will become "
                   "unavailable in a couple of releases.");
  return filterx_boolean_new(TRUE);
}

FILTERX_DEFINE_TYPE(metrics_labels, FILTERX_TYPE_NAME(dict),
                    .is_mutable = TRUE,
                    .truthy = _truthy,
                    .marshal = _marshal,
                    .repr = _repr,
                    .clone = _clone,
                    .free_fn = _free,
                   );
