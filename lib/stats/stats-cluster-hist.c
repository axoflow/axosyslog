/*
 * Copyright (c) 2025 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#include "stats/stats-cluster-hist.h"
#include "stats/stats-cluster.h"

static void
_counter_group_hist_free(StatsCounterGroup *counter_group)
{
  g_strfreev(counter_group->counter_names);
  g_free(counter_group->counters);
}

static gboolean
_counter_group_hist_get_type_label(StatsCounterGroup *self, StatsCluster *cluster, gint type, StatsClusterLabel *label)
{
  if (type < SC_TYPE_HIST_BUCKET0 || type >= SC_TYPE_HIST_MAX)
    return FALSE;

  const gchar *type_name = self->counter_names[type];
  *label = stats_cluster_label("le", type_name);
  return TRUE;
}

static void
_counter_group_hist_get_type_formatting(StatsCounterGroup *self,
                                        StatsCluster *cluster, gint type,
                                        StatsClusterUnit *stored_unit,
                                        StatsClusterFrameOfReference *frame_of_reference)
{
  if (type == SC_TYPE_HIST_SUM)
    return;

  *stored_unit = SCU_NONE;
  *frame_of_reference = SCFOR_NONE;
}

static const gchar *
_counter_group_hist_get_type_name_suffix(StatsCounterGroup *self, StatsCluster *cluster, gint type)
{
  if (type >= SC_TYPE_HIST_MAX)
    return NULL;

  if (type == SC_TYPE_HIST_SUM)
    return "_sum";
  if (type == SC_TYPE_HIST_COUNT)
    return "_count";

  return "_bucket";
}

static void
_counter_group_hist_init(StatsCounterGroupInit *self, StatsCounterGroup *counter_group)
{
  counter_group->counters = g_new0(StatsCounterItem, SC_TYPE_HIST_MAX);
  counter_group->capacity = SC_TYPE_HIST_MAX;
  counter_group->counter_names = g_strdupv(self->counter.names);
  counter_group->get_type_label = _counter_group_hist_get_type_label;
  counter_group->get_type_name_suffix = _counter_group_hist_get_type_name_suffix;
  counter_group->get_type_formatting = _counter_group_hist_get_type_formatting;
  counter_group->free_fn = _counter_group_hist_free;
}

static gboolean
_counter_group_hist_equals(const StatsCounterGroupInit *self, const StatsCounterGroupInit *other)
{
  return (self->init == other->init);
}


static void
_counter_group_hist_clone(StatsCounterGroupInit *dst, const StatsCounterGroupInit *src)
{
  dst->counter.names = g_strdupv(src->counter.names);

  dst->init = src->init;
  dst->equals = src->equals;
  dst->clone = src->clone;
  dst->cloned_free = src->cloned_free;
}

static void
_counter_group_hist_cloned_free(StatsCounterGroupInit *self)
{
  if (self->counter.names)
    g_strfreev(self->counter.names);
}

void
stats_cluster_hist_key_set(StatsClusterKey *key, const gchar *name, StatsClusterLabel *labels, gsize labels_len)
{
  stats_cluster_key_set(key, name, labels, labels_len, (StatsCounterGroupInit)
  {
    .counter.names = NULL,
    .init = _counter_group_hist_init,
    .equals = _counter_group_hist_equals,
    .clone = _counter_group_hist_clone,
    .cloned_free = _counter_group_hist_cloned_free,
  });
}

void
stats_cluster_hist_key_add_legacy_alias(StatsClusterKey *key, guint16 component, const gchar *id,
                                        const gchar *instance)
{
  stats_cluster_key_add_legacy_alias(key, component, id, instance, (StatsCounterGroupInit)
  {
    .counter.names = NULL,
    .init = _counter_group_hist_init,
    .equals = _counter_group_hist_equals,
    .clone = _counter_group_hist_clone,
    .cloned_free = _counter_group_hist_cloned_free,
  });
}
