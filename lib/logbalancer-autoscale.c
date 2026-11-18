/*
 * Copyright (c) 2026 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#include "logbalancer-autoscale.h"
#include "timeutils/misc.h"

#include <math.h>

#define PARTITION_STATS_HALFLIFE (30.0)
/* 10 half-life period is considered inactive (rate has already decayed to zero) */
#define PARTITION_EXPIRATION_INTERVAL ((gint) (PARTITION_STATS_HALFLIFE * 10))
#define PARTITION_RESCALE_INTERVAL (10)

typedef struct _Partition
{
  gdouble rate;
  struct timespec last_update;
  gint target;
  gint num_targets;
} Partition;

struct _LogBalancerAutoscale
{
  gint num_targets;
  gint wfo;

  /* protects the members below */
  GMutex lock;
  struct timespec last_rescale;
  GHashTable *partitions;
  GPtrArray *orphans;
  StatsCounterItem *active_partitions;
};

static inline gdouble
_get_time_diff_sec(const struct timespec *last_update, const struct timespec *now)
{
  gint64 diff = 0;
  if (last_update->tv_sec != 0)
    diff = timespec_diff_nsec(now, last_update);

  return (gdouble) diff / 1e9;
}

static inline gdouble
_apply_exp_decay(gdouble dt, gdouble *value)
{
  /* log(0.5) == -ln(2) */
  gdouble decay = exp(-G_LN2 * dt / (gdouble) PARTITION_STATS_HALFLIFE);
  *value *= decay;

  return decay;
}

static inline void
_apply_instant_rate(gint sample, gdouble dt, gdouble decay, gdouble *result)
{
  if (dt > 0)
    *result += sample/dt * (1.0 - decay);
}

static inline gdouble
_get_partition_rate_ratio(gdouble partition_rate, gdouble total_rate)
{
  return total_rate > 0.0 ? MIN(1.0, partition_rate / total_rate) : 0.0;
}

/* lock must be held when calling this method */
static inline gboolean
_is_rescale_due(LogBalancerAutoscale *self, const struct timespec *now)
{
  if (self->last_rescale.tv_sec == 0)
    return TRUE;

  gint64 diff = timespec_diff_usec(now, &self->last_rescale);
  return diff >= PARTITION_RESCALE_INTERVAL * 1000000;
}

/* lock must be held when calling this method */
static inline gboolean
_remove_if_partition_expired(Partition *p, GHashTableIter *iter, const struct timespec *now)
{
  gint64 diff = 0;
  if (p->last_update.tv_sec != 0)
    diff = timespec_diff_usec(now, &p->last_update);

  if (diff >= PARTITION_EXPIRATION_INTERVAL * 1000000)
    {
      g_hash_table_iter_remove(iter);
      return TRUE;
    }

  return FALSE;
}

/* lock must be held when calling this method */
static inline gdouble
_get_total_rate_and_update_partition_stats(LogBalancerAutoscale *self, Partition *current_partition,
                                           const struct timespec *now)
{
  gdouble total_rate = 0;

  GHashTableIter iter;
  gpointer k, v;
  g_hash_table_iter_init(&iter, self->partitions);
  while (g_hash_table_iter_next(&iter, &k, &v))
    {
      Partition *p = v;

      if (p != current_partition && _remove_if_partition_expired(p, &iter, now))
        continue;

      /* update all the other partitions' rate */
      if (p != current_partition)
        {
          gdouble dt = _get_time_diff_sec(&p->last_update, now);
          _apply_exp_decay(dt, &p->rate);
        }

      total_rate += p->rate;
    }

  return total_rate;
}

/* lock must be held when calling this method */
static inline void
_rescale(LogBalancerAutoscale *self, Partition *current_partition, const struct timespec *now)
{
  gdouble total_rate = _get_total_rate_and_update_partition_stats(self, current_partition, now);
  gint num_targets = self->num_targets;
  gint targets_used = 0;

  /* reserve WFO number of targets for minuscule partitions */
  gint free_targets = MAX(1, num_targets - self->wfo);

  /*
   * We need to round down the fractional part of the targets assigned to one partition,
   * so we do not exceed the total number of targets available.
   * We can store the unused fractional part though and carry it over to the next partition.
   */
  gdouble fractional_target_carryover = 0.0;

  gint current_target = 0;
  g_ptr_array_set_size(self->orphans, 0);

  Partition *part;
  GHashTableIter iter;
  gpointer k, v;
  g_hash_table_iter_init(&iter, self->partitions);
  while (g_hash_table_iter_next(&iter, &k, &v))
    {
      part = v;

      part->target = current_target;

      gdouble partition_ratio = _get_partition_rate_ratio(part->rate, total_rate);
      gdouble partition_targets = partition_ratio * free_targets + fractional_target_carryover;
      part->num_targets = (gint) (0 + floor(partition_targets));
      fractional_target_carryover = partition_targets - part->num_targets;
      targets_used += part->num_targets;

      if (part->num_targets <= 0)
        g_ptr_array_add(self->orphans, part);

      current_target = (current_target + part->num_targets) % num_targets;
    }

  gdouble remaining_targets = num_targets - targets_used;

  for (gint i = 0; i < self->orphans->len; ++i)
    {
      Partition *p = g_ptr_array_index(self->orphans, i);
      p->target = current_target;
      p->num_targets = (gint) MAX(1.0, remaining_targets / self->orphans->len);

      remaining_targets -= p->num_targets;

      if (remaining_targets >= 1)
        current_target = (current_target + p->num_targets) % num_targets;
    }

  g_ptr_array_set_size(self->orphans, 0);

  self->last_rescale = *now;

  stats_counter_set(self->active_partitions, g_hash_table_size(self->partitions));
}

/* lock must be held when calling this method */
static inline void
_update_partition_stats(Partition *partition, const struct timespec *now)
{
  const gint num_of_messages = 1;

  gdouble partition_dt = _get_time_diff_sec(&partition->last_update, now);

  /* first message should be considered too */
  if (partition_dt <= 0)
    partition_dt = 1;

  /* exponentially weighted moving average */
  gdouble partition_decay = _apply_exp_decay(partition_dt, &partition->rate);
  _apply_instant_rate(num_of_messages, partition_dt, partition_decay, &partition->rate);

  partition->last_update = *now;
}

gint
log_balancer_autoscale_select_at(LogBalancerAutoscale *self, const gchar *partition_key, LogMessage *msg,
                                 const struct timespec *now)
{
  g_mutex_lock(&self->lock);

  Partition *partition;
  gboolean new_partition = FALSE;
  if (!g_hash_table_lookup_extended(self->partitions, partition_key, NULL, (gpointer *) &partition))
    {
      partition = g_new0(Partition, 1);
      g_hash_table_insert(self->partitions, g_strdup(partition_key), partition);
      new_partition = TRUE;
    }

  _update_partition_stats(partition, now);

  if (new_partition || _is_rescale_due(self, now))
    _rescale(self, partition, now);

  gint selected_target;
  if (partition->num_targets == 1)
    {
      selected_target = partition->target;
    }
  else
    {
      guint spread = (gint64) msg->timestamps[LM_TS_RECVD].ut_usec * partition->num_targets / 1000000LL;
      selected_target = (partition->target + spread) % self->num_targets;
    }

  g_mutex_unlock(&self->lock);
  return selected_target;
}

gint
log_balancer_autoscale_select(LogBalancerAutoscale *self, const gchar *partition_key, LogMessage *msg)
{
  struct timespec now = {0};
  clock_gettime(CLOCK_MONOTONIC, &now);

  return log_balancer_autoscale_select_at(self, partition_key, msg, &now);
}

void
log_balancer_autoscale_set_active_partitions_counter(LogBalancerAutoscale *self, StatsCounterItem *counter)
{
  g_mutex_lock(&self->lock);
  self->active_partitions = counter;
  g_mutex_unlock(&self->lock);
}

gint
log_balancer_autoscale_get_num_partitions(LogBalancerAutoscale *self)
{
  g_mutex_lock(&self->lock);
  gint num_partitions = g_hash_table_size(self->partitions);
  g_mutex_unlock(&self->lock);
  return num_partitions;
}

LogBalancerAutoscale *
log_balancer_autoscale_new(gint num_targets, gint wfo)
{
  LogBalancerAutoscale *self = g_new0(LogBalancerAutoscale, 1);

  self->num_targets = num_targets;
  self->wfo = wfo;
  g_mutex_init(&self->lock);
  self->partitions = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
  self->orphans = g_ptr_array_new_full(10, NULL);
  return self;
}

void
log_balancer_autoscale_free(LogBalancerAutoscale *self)
{
  g_hash_table_destroy(self->partitions);
  g_ptr_array_free(self->orphans, TRUE);
  g_mutex_clear(&self->lock);
  g_free(self);
}
