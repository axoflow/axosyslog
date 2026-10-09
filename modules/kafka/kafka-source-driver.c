/*
 * Copyright (c) 2025 Hofi <hofione@gmail.com>
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

/*
 * Original implementation sourced from https://github.com/syslog-ng/syslog-ng/pull/5564.
 * The license has been upgraded from GPL-2.0-or-later to GPL-3.0-or-later.
 */

#include "kafka-source-driver.h"
#include "kafka-source-worker.h"
#include "kafka-internal.h"
#include "kafka-props.h"
#include "kafka-topic-parts.h"
#include "stats/stats-cluster-single.h"
#include "stats/aggregator/stats-aggregator-registry.h"
#include "host-id.h"

// TODO: Move these to a common lib place
GList *
g_list_remove_duplicates(GList *list, GEqualFunc compare_func, GDestroyNotify free_func)
{
  g_assert(list && compare_func);

  for (GList *outer = list; outer != NULL; outer = outer->next)
    {
      GList *inner = g_list_last(list);

      while (inner != NULL && inner != outer)
        {
          GList *prev = inner->prev;

          if (compare_func(outer->data, inner->data))
            {
              if (free_func)
                free_func(inner->data);
              list = g_list_delete_link(list, inner);
            }

          inner = prev;
        }
    }
  return list;
}

gboolean
g_int32_equal(gconstpointer  v1, gconstpointer  v2)
{
  return (int32_t)GPOINTER_TO_INT(v1) == (int32_t)GPOINTER_TO_INT(v2);
}

gboolean
g_int32_compare(gconstpointer  v1, gconstpointer  v2)
{
  if ((int32_t)GPOINTER_TO_INT(v1) < (int32_t)GPOINTER_TO_INT(v2))
    return -1;
  else if ((int32_t)GPOINTER_TO_INT(v1) > (int32_t)GPOINTER_TO_INT(v2))
    return 1;
  else
    return 0;
}

typedef gboolean (*ListItemConvertFunc)(const gchar *token, gpointer *item);
typedef gboolean (*ListItemValidateFunc)(gpointer token, GError **error);

static GList *
_split_to_list(const gchar *name, const gchar *input, const gchar *split_str,
               gboolean break_on_validate_err,
               GDestroyNotify free_func, ListItemConvertFunc convert_func, ListItemValidateFunc validate_func)
{
  g_assert(input);
  if (*input == 0)
    {
      msg_error("kafka: list cannot be an empty string", evt_tag_str("name", name));
      return NULL;
    }

  GList *list = NULL;
  gchar *trimmed = NULL;
  int item_ndx = 0;
  gchar **tokens = g_strsplit(input, split_str, -1);

  for (; tokens[item_ndx] != NULL; item_ndx++)
    {
      trimmed = g_strstrip(tokens[item_ndx]);
      if (*trimmed == 0)
        goto err_exit;
      else
        {
          gpointer item = NULL;
          if (FALSE == convert_func(trimmed, &item))
            goto maybe_exit;
          else
            {
              if (validate_func)
                {
                  if (validate_func(item, NULL))
                    list = g_list_prepend(list, item);
                  else
                    {
                      if (free_func)
                        free_func(item);
                      goto maybe_exit;
                    }
                }
              else
                list = g_list_prepend(list, item);
            }
        }
      continue;
maybe_exit:
      if (break_on_validate_err)
        goto err_exit;
      else
        msg_warning("kafka: skipping invalid list item value", evt_tag_str("name", name), evt_tag_int("item_ndx", item_ndx),
                    evt_tag_str("value", trimmed));
    }
  g_strfreev(tokens);

  return list;

err_exit:
  msg_error("kafka: invalid list item value", evt_tag_str("name", name), evt_tag_int("item_ndx", item_ndx),
            evt_tag_str("value", trimmed));
  g_strfreev(tokens);
  if (list)
    g_list_free(list);
  return NULL;
}

static gboolean
_convert_to_int(const gchar *token, gpointer *item)
{
  char *endptr;
  long val = strtol(token, &endptr, 10);

  if (*endptr != '\0')
    {
      *item = NULL;
      return FALSE;
    }
  *item = GINT_TO_POINTER((gint)(int32_t) val);
  return TRUE;
}

static gboolean
_validate_part_num(gpointer item, GError **error)
{
  int32_t part_num = (int32_t)GPOINTER_TO_INT(item);
  return part_num >= RD_KAFKA_PARTITION_UA;
}

static gboolean
_is_topic_pattern(const char *topic)
{
  return FALSE == kafka_validate_topic_name(topic, NULL);
}

static gboolean
_validate_topic_name(gpointer item, GError **error)
{
  const gchar *topic = (const gchar *)item;

  if (error)
    *error = NULL;
  if (FALSE == kafka_validate_topic_name(topic, NULL))
    return kafka_validate_topic_pattern(topic, error);
  return TRUE;
}

static void
_format_stats_key(LogThreadedSourceDriver *d, StatsClusterKeyBuilder *kb)
{
  KafkaSourceDriver *self = (KafkaSourceDriver *)d;

  stats_cluster_key_builder_add_legacy_label(kb, stats_cluster_label("driver", "kafka"));
  if (self->group_id)
    stats_cluster_key_builder_add_legacy_label(kb, stats_cluster_label("group_id", self->group_id));
}

static const gchar *
_format_persist_name(const LogPipe *d)
{
  const KafkaSourceDriver *self = (const KafkaSourceDriver *)d;
  static gchar persist_name[1024];

  if (d->persist_name)
    g_snprintf(persist_name, sizeof(persist_name), "kafka.%s", d->persist_name);
  else
    g_snprintf(persist_name, sizeof(persist_name), "kafka(%s)", self->super.super.super.id);
  return persist_name;
}

void
kafka_sd_update_msg_length_stats(KafkaSourceDriver *self, gsize len)
{
  stats_aggregator_add_data_point(self->max_message_size, len);
  stats_aggregator_add_data_point(self->average_messages_size, len);
}

void
kafka_sd_inc_msg_topic_stats(KafkaSourceDriver *self, const gchar *topic)
{
  StatsCounterItem *counter = g_hash_table_lookup(self->stats_topics, topic);
  stats_counter_inc(counter);
}

static inline const gchar *
_worker_get_name(KafkaSourceDriver *self, gint index)
{
  if (FALSE == self->options.separated_worker_queues && kafka_sd_parallel_processing(self))
    return self->single_queue_name;
  else
    return kafka_src_worker_get_name(self->super.workers[index]);
}

void
kafka_sd_update_msg_worker_stats(KafkaSourceDriver *self, gint worker_ndx)
{
  StatsCounterItem *counter = g_hash_table_lookup(self->stats_workers, _worker_get_name(self, worker_ndx));
  guint msg_queue_len = g_async_queue_length(self->msg_queues[self->options.separated_worker_queues ? worker_ndx : 1]);
  stats_counter_set(counter, msg_queue_len);
}

static void
_register_worker_stats(KafkaSourceDriver *self)
{
  const gchar *counter_names[] = { "queued", NULL };

  /* Intentionally not using the 0 index slot */
  for (int i = 1 ; i < self->allocated_queue_num; i++)
    {
      const gchar *label_value_ptr = _worker_get_name(self, i);
      if (FALSE == self->options.separated_worker_queues && kafka_sd_parallel_processing(self))
        {
          g_snprintf(self->single_queue_name, sizeof(self->single_queue_name), "%s-%d",
                     kafka_src_worker_get_name(self->super.workers[1]),
                     self->super.num_workers - 1);
          label_value_ptr = self->single_queue_name;
        }
      if (FALSE == g_hash_table_contains(self->stats_workers, label_value_ptr))
        kafka_register_counters(self, self->stats_workers, "worker", label_value_ptr, counter_names, STATS_LEVEL2);
    }
}

static void
_unregister_worker_counters(gpointer key, gpointer value, gpointer user_data)
{
  KafkaSourceDriver *self = (KafkaSourceDriver *)user_data;
  const gchar *worker_id = (const gchar *)key;
  StatsCounterItem *counter = (StatsCounterItem *)value;
  const gchar *counter_names[] = { "queued", NULL };

  kafka_unregister_counters(self, "worker", worker_id, counter, counter_names);
}

static void
_unregister_worker_stats(KafkaSourceDriver *self)
{
  g_hash_table_foreach(self->stats_workers, _unregister_worker_counters, self);
}

static void
_unregister_topic_counters(gpointer key, gpointer value, gpointer user_data)
{
  KafkaSourceDriver *self = (KafkaSourceDriver *)user_data;
  const gchar *topic = (const gchar *)key;
  StatsCounterItem *counter = (StatsCounterItem *)value;
  const gchar *counter_names[] = { "processed", NULL };

  kafka_unregister_counters(self, "topic", topic, counter, counter_names);
}

static inline void
_unregister_topic_stats(KafkaSourceDriver *self)
{
  g_hash_table_foreach(self->stats_topics, _unregister_topic_counters, self);
}

static void
_register_aggregated_stats(KafkaSourceDriver *self)
{
  LogThreadedSourceWorker *worker = self->super.workers[0];
  StatsClusterKeyBuilder *kb = worker->super.metrics.stats_kb;
  stats_cluster_key_builder_push(kb);
  {
    LogSourceOptions *super_options = &self->options.worker_options->super;
    gchar *stats_id = worker->super.stats_id;
    gchar stats_instance[1024];
    const gchar *instance_name = stats_cluster_key_builder_format_legacy_stats_instance(kb, stats_instance,
                                 sizeof(stats_instance));
    stats_aggregator_lock();
    StatsClusterKey sc_key;

    stats_cluster_single_key_legacy_set_with_name(&sc_key,
                                                  super_options->stats_source | SCS_SOURCE,
                                                  stats_id,
                                                  instance_name, "msg_size_max");
    stats_register_aggregator_maximum(super_options->stats_level, &sc_key,
                                      &self->max_message_size);

    stats_cluster_single_key_legacy_set_with_name(&sc_key,
                                                  super_options->stats_source | SCS_SOURCE,
                                                  stats_id,
                                                  instance_name, "msg_size_avg");
    stats_register_aggregator_average(super_options->stats_level, &sc_key,
                                      &self->average_messages_size);

    stats_cluster_single_key_legacy_set_with_name(&sc_key,
                                                  super_options->stats_source | SCS_SOURCE,
                                                  stats_id,
                                                  instance_name, "eps");
    stats_register_aggregator_cps(super_options->stats_level, &sc_key,
                                  worker->super.metrics.recvd_messages_key,
                                  SC_TYPE_SINGLE_VALUE,
                                  &self->CPS);
    stats_aggregator_unlock();
  }
  stats_cluster_key_builder_pop(kb);
}

static void
_unregister_aggregated_stats(KafkaSourceDriver *self)
{
  stats_aggregator_lock();

  stats_unregister_aggregator(&self->max_message_size);
  stats_unregister_aggregator(&self->average_messages_size);
  stats_unregister_aggregator(&self->CPS);

  stats_aggregator_unlock();
}

static gboolean
_has_wildcard_topic_or_partition(GList *requested_topics,
                                 gboolean *has_wildcard_topic,
                                 gboolean *has_wildcard_partition)
{
  g_assert(g_list_length(requested_topics));

  for (GList *l = requested_topics; l != NULL; l = l->next)
    {
      KafkaTopicParts *item = (KafkaTopicParts *)l->data;
      if (_is_topic_pattern(item->topic))
        *has_wildcard_topic = TRUE;

      for (GList *p = item->partitions; p != NULL; p = p->next)
        {
          int32_t partition = (int32_t)GPOINTER_TO_INT(p->data);
          if (partition == RD_KAFKA_PARTITION_UA)
            *has_wildcard_partition = TRUE;
        }
      if (*has_wildcard_topic && *has_wildcard_partition)
        break;
    }
  return *has_wildcard_partition || *has_wildcard_topic;
}

static void
_decide_strategy(KafkaSourceDriver *self)
{
  gboolean wildcard_topic = FALSE, wildcard_partition = FALSE;

  _has_wildcard_topic_or_partition(self->requested_topics, &wildcard_topic, &wildcard_partition);

  if (wildcard_partition || wildcard_topic || self->options.strategy_hint == KSCS_SUBSCRIBE)
    self->strategy = KSCS_SUBSCRIBE;
  else
    self->strategy = KSCS_ASSIGN;

  msg_verbose("kafka: selected consumer strategy",
              evt_tag_str("strategy_hint", self->options.strategy_hint == KSCS_SUBSCRIBE ? "subscribe" : "assign"),
              evt_tag_str("strategy", self->strategy == KSCS_SUBSCRIBE ? "subscribe" : "assign"),
              evt_tag_str("group_id", self->group_id),
              evt_tag_str("driver", self->super.super.super.id));
}

static void
_kafka_log_state_changed(KafkaSourceDriver *self, KafkaConnectedState state, rd_kafka_resp_err_t err,
                         const char *reason)
{
  const char *state_str;
  switch (state)
    {
    case KFS_CONNECTED:
      state_str = "CONNECTED";
      break;
    case KFS_DISCONNECTED:
      state_str = "DISCONNECTED";
      break;
    default:
      g_assert_not_reached();
    }

  msg_verbose("kafka: current state changed",
              evt_tag_str("state", state_str),
              evt_tag_str("group_id", self->group_id),
              evt_tag_str("driver", self->super.super.super.id));
  if (state == KFS_DISCONNECTED)
    msg_verbose("kafka: Temporally error occured",
                evt_tag_str("error", reason ? reason : rd_kafka_err2str(err)),
                evt_tag_str("driver", self->super.super.super.id));
}

rd_kafka_resp_err_t
kafka_update_state(KafkaSourceDriver *self, gboolean lock)
{
  if (self->kafka == NULL)
    return RD_KAFKA_RESP_ERR__STATE;

  if (lock)
    kafka_opaque_state_lock(&self->opaque);

  KafkaConnectedState state = kafka_opaque_state_get(&self->opaque);

  const struct rd_kafka_metadata *metadata;
  rd_kafka_resp_err_t err = rd_kafka_metadata(self->kafka, 0, NULL, &metadata,
                                              self->options.super.state_update_timeout);
  if (err == RD_KAFKA_RESP_ERR_NO_ERROR)
    {
      KafkaConnectedState prev_state = state;

      state = KFS_CONNECTED;
      kafka_opaque_state_set(&self->opaque, state);
      kafka_opaque_state_set_last_error(&self->opaque, 0);

      if (prev_state != state)
        {
          _kafka_log_state_changed(self, state, err, NULL);
          kafka_sd_wakeup_kafka_queues(self);
        }
      rd_kafka_metadata_destroy(metadata);
    }
  else
    {
      /* Though, the error can be RD_KAFKA_RESP_ERR__TIMED_OUT as well, treat it as not connected too on startup */
      if (state == KFS_UNKNOWN)
        {
          state = KFS_DISCONNECTED;
          kafka_opaque_state_set(&self->opaque, state);
          kafka_opaque_state_set_last_error(&self->opaque, err);
          _kafka_log_state_changed(self, state, err, NULL);
        }
    }

  if (lock)
    kafka_opaque_state_unlock(&self->opaque);

  return err;
}

inline guint
kafka_sd_used_queue_num(KafkaSourceDriver *self)
{
  return self->allocated_queue_num - 1;
}

inline gboolean
kafka_sd_using_queues(KafkaSourceDriver *self)
{
  return self->allocated_queue_num > 0;
}

inline gboolean
kafka_sd_parallel_processing(KafkaSourceDriver *self)
{
  return self->super.num_workers > 2;
}

static void
_alloc_msg_queues(KafkaSourceDriver *self)
{
  g_assert(self->msg_queues == NULL);
  if (self->super.num_workers <= 1)
    return;

  /* Intentionally not using the 0 index slot */
  self->allocated_queue_num = (self->options.separated_worker_queues ? self->super.num_workers : 2);

  self->msg_queues = g_new0(GAsyncQueue *, self->allocated_queue_num);
  self->queue_cond_mutexes = g_new0(GMutex, self->allocated_queue_num);
  self->queue_conds = g_new0(GCond, self->allocated_queue_num);

  /* Intentionally not using the 0 index slot */
  for (guint i = 1; i < self->allocated_queue_num; ++i)
    {
      self->msg_queues[i] = g_async_queue_new();
      g_mutex_init(&self->queue_cond_mutexes[i]);
      g_cond_init(&self->queue_conds[i]);
    }
}

static void
_destroy_msg_queues(KafkaSourceDriver *self)
{
  if (self->msg_queues == NULL)
    return;

  /* Intentionally not using the 0 index slot */
  for (guint i = 1; i < self->allocated_queue_num; ++i)
    {
      GAsyncQueue *queue = self->msg_queues[i];
      g_assert(g_async_queue_length(queue) == 0);
      g_async_queue_unref(queue);

      g_mutex_clear(&self->queue_cond_mutexes[i]);
      g_cond_clear(&self->queue_conds[i]);
    }
  g_free(self->msg_queues);
  self->msg_queues = NULL;
  g_free(self->queue_cond_mutexes);
  self->queue_cond_mutexes = NULL;
  g_free(self->queue_conds);
  self->queue_conds = NULL;
  self->allocated_queue_num = 0;
}

inline GAsyncQueue *
kafka_sd_worker_queue(KafkaSourceDriver *self, LogThreadedSourceWorker *worker)
{
  /* Intentionally not using the 0 index slot */
  guint ndx = (self->options.separated_worker_queues ? worker->worker_index : 1);
  return self->msg_queues[ndx];
}

void
kafka_sd_wait_for_queue(KafkaSourceDriver *self, LogThreadedSourceWorker *worker)
{
  /* Intentionally not using the 0 index slot */
  guint ndx = (self->options.separated_worker_queues ? worker->worker_index : 1);
  g_mutex_lock(&self->queue_cond_mutexes[ndx]);
  g_atomic_counter_inc(&self->sleeping_thread_num);
  g_cond_wait(&self->queue_conds[ndx], &self->queue_cond_mutexes[ndx]);
  g_atomic_counter_dec_and_test(&self->sleeping_thread_num);
  g_mutex_unlock(&self->queue_cond_mutexes[ndx]);
}


static inline gboolean
_kafka_sd_all_workers_exited(KafkaSourceDriver *self)
{
  return g_atomic_counter_get(&self->running_thread_num) <=
         1; /* If no workers are started ever, even not the main one, this can be 0 as well */
}

void
kafka_sd_wait_for_queue_processors_to_exit(KafkaSourceDriver *self, const gdouble iteration_sleep_time)
{
  kafka_msg_trace("kafka: waiting for queue processors to exit",
                  evt_tag_str("group_id", self->group_id),
                  evt_tag_str("driver", self->super.super.super.id));
  kafka_sd_signal_queues(self);

  while (FALSE == _kafka_sd_all_workers_exited(self))
    {
      main_loop_worker_wait_for_exit_until(iteration_sleep_time);
      kafka_sd_signal_queues(self);
    }
}

static inline gboolean
kafka_sd_all_workers_sleeping(KafkaSourceDriver *self)
{
  return g_atomic_counter_get(&self->sleeping_thread_num) == self->super.num_workers - 1;
}

gboolean
kafka_sd_wait_for_queue_processors_to_sleep(KafkaSourceDriver *self, const gdouble iteration_sleep_time,
                                            gboolean poll_kafka)
{
  kafka_msg_trace("kafka: waiting for queue processors to sleep",
                  evt_tag_str("group_id", self->group_id),
                  evt_tag_str("driver", self->super.super.super.id));

  while (FALSE == kafka_sd_all_workers_sleeping(self) && FALSE == _kafka_sd_all_workers_exited(self))
    {
      if (poll_kafka)
        kafka_update_state(self, TRUE);

      if (main_loop_worker_wait_for_exit_until(iteration_sleep_time))
        return FALSE;
    }
  return TRUE;
}

inline void
kafka_sd_signal_queue_ndx(KafkaSourceDriver *self, guint ndx)
{
  g_cond_signal(&self->queue_conds[ndx]);
}

inline void
kafka_sd_signal_queue(KafkaSourceDriver *self, LogThreadedSourceWorker *worker)
{
  /* Intentionally not using the 0 index slot */
  guint ndx = (self->options.separated_worker_queues ? worker->worker_index : 1);
  kafka_sd_signal_queue_ndx(self, ndx);
}

inline void
kafka_sd_signal_queues(KafkaSourceDriver *self)
{
  /* Intentionally not using the 0 index slot */
  for (guint i = 1; i < self->allocated_queue_num; ++i)
    kafka_sd_signal_queue_ndx(self, i);
}

/* Lazy check for empty queues */
inline guint
kafka_sd_worker_queues_len(KafkaSourceDriver *self)
{
  guint len = 0;
  /* Intentionally not using the 0 index slot */
  for (guint i = 1; i < self->allocated_queue_num; ++i)
    len += g_async_queue_length(self->msg_queues[i]);
  return len;
}

void
kafka_sd_drop_queued_messages(KafkaSourceDriver *self)
{
  kafka_msg_debug("kafka: dropping queued messages",
                  evt_tag_str("group_id", self->group_id),
                  evt_tag_str("driver", self->super.super.super.id),
                  evt_tag_int("worker_queues_len", kafka_sd_worker_queues_len(self)));
  rd_kafka_message_t *msg;

  /* Intentionally not using the 0 index slot */
  for (guint i = 1; i < self->allocated_queue_num; ++i)
    {
      GAsyncQueue *msg_queue = self->msg_queues[i];
      while ((msg = g_async_queue_try_pop(msg_queue)) != NULL)
        rd_kafka_message_destroy(msg);
      kafka_sd_update_msg_worker_stats(self, i);
    }
  g_assert(kafka_sd_worker_queues_len(self) == 0);
}

void
kafka_sd_wakeup_kafka_queues(KafkaSourceDriver *self)
{
#if SYSLOG_NG_HAVE_RD_KAFKA_QUEUE_YIELD
  if (self->consumer_kafka_queue)
    rd_kafka_queue_yield(self->consumer_kafka_queue);
  if (self->main_kafka_queue)
    rd_kafka_queue_yield(self->main_kafka_queue);
#else
  msg_warning("kafka: rd_kafka_queue_yield() is not available in the linked librdkafka version, syslog-ng shutdown latency may increase to `poll_timeout` value",
              evt_tag_str("driver", self->super.super.super.id));
#endif
}

static gboolean
_check_and_sort_partitions(KafkaSourceDriver *self, const gchar *partitions, GList **requested_partitions)
{
  GList *list = _split_to_list("partition", partitions, ",", TRUE, NULL,
                               _convert_to_int, _validate_part_num);
  if (list == NULL)
    return FALSE;

  list = g_list_sort(list, g_int32_compare);
  const gint original_length = g_list_length(list);
  if (list)
    list = g_list_remove_duplicates(list, g_int32_equal, NULL);

  const gpointer all_parts = GINT_TO_POINTER((gint)RD_KAFKA_PARTITION_UA);
  if (g_list_find(list, all_parts))
    {
      if (g_list_length(list) > 1)
        {
          msg_error("kafka: error, 'all partitions' (-1) specified along with other partition numbers",
                    evt_tag_str("partitions", partitions));
          g_list_free(list);
          return FALSE;
        }
    }

  if (requested_partitions)
    {
      if (original_length != g_list_length(list))
        msg_warning("kafka: dropped duplicated entries of partition numbers",
                    evt_tag_str("partitions", partitions),
                    evt_tag_str("driver", self->super.super.super.id));
      *requested_partitions = list;
    }
  return TRUE;
}

static gboolean
_check_and_apply_topics(KafkaSourceDriver *self, GList *topics, gboolean apply)
{
  if (g_list_length(topics) == 0)
    return FALSE;

  GList *requested_topics = NULL;
  for (GList *topic_option = topics; topic_option != NULL; topic_option = topic_option->next)
    {
      GList *requested_partitions = NULL;
      KafkaProperty *prop = (KafkaProperty *)topic_option->data;

      if (_check_and_sort_partitions(self, (const gchar *) prop->value, &requested_partitions))
        {
          GError *err = NULL;
          if (_validate_topic_name(prop->name, &err))
            {
              KafkaTopicParts *new_item = kafka_tps_new(prop->name, requested_partitions);
              requested_topics = g_list_prepend(requested_topics, new_item);
              continue;
            }
          else
            {
              msg_error("kafka: invalid topic name in the requested topics list",
                        evt_tag_str("topic", prop->name),
                        evt_tag_str("error", err ? err->message : "(unknown)"),
                        evt_tag_str("driver", self->super.super.super.id));
              if (err)
                g_error_free(err);
            }
        }
      if (requested_partitions)
        g_list_free(requested_partitions);
      if (requested_topics)
        kafka_tps_list_free(requested_topics);
      return FALSE;
    }
  g_assert(requested_topics != NULL);

  const gint original_length = g_list_length(requested_topics);
  requested_topics = g_list_remove_duplicates(requested_topics, kafka_tps_equal, (GDestroyNotify) kafka_tps_free);

  if (apply)
    {
      if (original_length != g_list_length(requested_topics))
        msg_warning("kafka: dropped duplicated entries of topics config option",
                    evt_tag_str("driver", self->super.super.super.id));
      if (self->requested_topics)
        kafka_tps_list_free(self->requested_topics);
      self->requested_topics = requested_topics;
    }
  else if (requested_topics)
    kafka_tps_list_free(requested_topics);

  return TRUE;
}

gchar *
_get_unique_group_id(KafkaSourceDriver *self)
{
  GString *sb = g_string_new(self->super.super.super.id);
  host_id_append_formatted_id(sb, host_id_get());
  return g_string_free(sb, FALSE);
}

static void
_apply_group_id(KafkaSourceDriver *self)
{
  const gchar *group_id_key = "group.id";
  gchar *group_id = NULL;
  KafkaProperty *kp = kafka_property_list_find_not_empty(self->options.super.config, group_id_key);
  const gchar *conf_group_id = kp ? kp->value : NULL;

  /* Locally stored bookmarks could be inconsistent if multiple consumers share the same group.id as the rebalancing
   * process could assign the same partition to multiple consumers from multiple instances/clients, therefore we disable the use of
   * config group.id in this case by generating a unique one based on the driver ID + HOSTID.
   * NOTE: Actually, this condition could be more strict using `&& driver->strategy == KSCS_SUBSCRIBE`, but
           the strategy is not yet decided/applied at this point (and the decision cannot be moved now easily)
   */
  gboolean disable = (self->options.persist_store == KSPS_LOCAL);
  if (conf_group_id)
    {
      if (disable)
        {
          group_id = _get_unique_group_id(self);
          msg_warning("kafka: cannot use custom config group.id when using locally stored bookmarks, ignoring",
                      evt_tag_str("group_id", conf_group_id),
                      evt_tag_str("new_group_id", group_id),
                      evt_tag_str("driver", self->super.super.super.id));

          g_free(kp->value);
          kp->value = g_strdup(group_id);
        }
      else
        {
          group_id = g_strdup(conf_group_id);
          kafka_msg_debug("kafka: found config group.id",
                          evt_tag_str("group_id", group_id),
                          evt_tag_str("driver", self->super.super.super.id));
        }
    }
  else
    {
      group_id = _get_unique_group_id(self);
      kafka_msg_debug((disable ?
                       "kafka: using a self-generated group.id" :
                       "kafka: config group.id not found, using a self-generated one"),
                      evt_tag_str("group_id", group_id),
                      evt_tag_str("driver", self->super.super.super.id));

      KafkaProperty *kp_groupid = g_new0(KafkaProperty, 1);
      kp_groupid->name = g_strdup(group_id_key);
      kp_groupid->value = g_strdup(group_id);
      self->options.super.config = g_list_prepend(self->options.super.config, kp_groupid);
    }

  g_free(self->group_id);
  self->group_id = group_id;
}

static void
_apply_options(KafkaSourceDriver *self)
{
  _apply_group_id(self);
  _check_and_apply_topics(self, self->options.requested_topics, TRUE);
}

static gboolean
kafka_sd_init(LogPipe *s)
{
  KafkaSourceDriver *self = (KafkaSourceDriver *)s;

  if (self->options.requested_topics == NULL)
    {
      msg_error("kafka: the topic() argument is required for kafka source",
                evt_tag_str("driver", self->super.super.super.id),
                log_pipe_location_tag(&self->super.super.super.super));
      return FALSE;
    }

  if (self->options.super.bootstrap_servers == NULL)
    {
      msg_error("kafka: the bootstrap-servers() option is required for kafka source",
                evt_tag_str("driver", self->super.super.super.id),
                log_pipe_location_tag(&self->super.super.super.super));
      return FALSE;
    }

  _apply_options(self);
  _decide_strategy(self);

  _alloc_msg_queues(self);
  self->stats_topics = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  self->stats_workers = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  kafka_opaque_init(&self->opaque, &self->super.super.super, &self->options.super);

  if (FALSE == log_threaded_source_driver_init_method(s))
    return FALSE;

  _register_worker_stats(self);
  _register_aggregated_stats(self);

  msg_verbose("kafka: Kafka source initialized",
              evt_tag_str("group_id", self->group_id),
              evt_tag_str("driver", self->super.super.super.id),
              log_pipe_location_tag(&self->super.super.super.super));
  return TRUE;
}

static gboolean
kafka_sd_deinit(LogPipe *s)
{
  KafkaSourceDriver *self = (KafkaSourceDriver *)s;

  g_free(self->group_id);

  if (self->requested_topics)
    kafka_tps_list_free(self->requested_topics);

  kafka_opaque_deinit(&self->opaque);
  _destroy_msg_queues(self);

  _unregister_aggregated_stats(self);
  _unregister_worker_stats(self);
  _unregister_topic_stats(self);
  g_hash_table_destroy(self->stats_topics);
  g_hash_table_destroy(self->stats_workers);

  return log_threaded_source_driver_deinit_method(s);
}

static void
kafka_sd_free(LogPipe *s)
{
  KafkaSourceDriver *self = (KafkaSourceDriver *) s;

  kafka_sd_options_destroy(&self->options);

  log_threaded_source_driver_free_method(s);
}

LogDriver *
kafka_sd_new(GlobalConfig *cfg)
{
  KafkaSourceDriver *self = g_new0(KafkaSourceDriver, 1);
  log_threaded_source_driver_init_instance(&self->super, cfg);

  /* The default number of workers is best to set to a minimum of 2
   * to allow parallelism between fetching and processing messages,
   * even for the single_topic/single_partition KSCS_BATCH_CONSUME processing strategy.
   * User can override it later if needed.
   */
  self->super.num_workers = 2;

  self->super.super.super.super.init = kafka_sd_init;
  self->super.super.super.super.deinit = kafka_sd_deinit;
  self->super.super.super.super.free_fn = kafka_sd_free;
  self->super.super.super.super.generate_persist_name = _format_persist_name;

  self->super.worker_construct = kafka_src_worker_new;

  self->super.format_stats_key = _format_stats_key;

  kafka_sd_options_defaults(&self->options, &self->super.worker_options);

  return &self->super.super.super;
}

/* *********
 *  Options
 * *********/

void
kafka_sd_merge_config(LogDriver *d, GList *props)
{
  KafkaSourceDriver *self = (KafkaSourceDriver *)d;

  kafka_options_merge_config(&self->options.super, props);
}

gboolean
kafka_sd_set_logging(LogDriver *d, const gchar *logging)
{
  KafkaSourceDriver *self = (KafkaSourceDriver *)d;

  return kafka_options_set_logging(&self->options.super, logging);
}

void
kafka_sd_set_bootstrap_servers(LogDriver *d, const gchar *bootstrap_servers)
{
  KafkaSourceDriver *self = (KafkaSourceDriver *)d;

  kafka_options_set_bootstrap_servers(&self->options.super, bootstrap_servers);
}

gboolean
kafka_sd_set_topics(LogDriver *d, GList *topics)
{
  KafkaSourceDriver *self = (KafkaSourceDriver *) d;

  if (self->options.requested_topics)
    kafka_property_list_free(self->options.requested_topics);
  self->options.requested_topics = topics;

  return _check_and_apply_topics(self, topics, FALSE);
}

gboolean
kafka_sd_set_strategy_hint(LogDriver *d, const gchar *strategy_hint)
{
  KafkaSourceDriver *self = (KafkaSourceDriver *) d;

  if (g_strcmp0(strategy_hint, "subscribe") == 0)
    self->options.strategy_hint = KSCS_SUBSCRIBE;
  else if (g_strcmp0(strategy_hint, "assign") == 0)
    self->options.strategy_hint = KSCS_ASSIGN;
  else
    return FALSE;
  return TRUE;
}

gboolean
kafka_sd_set_persis_store(LogDriver *d, const gchar *persist_store)
{
  KafkaSourceDriver *self = (KafkaSourceDriver *) d;

  if (g_strcmp0(persist_store, "local") == 0)
    self->options.persist_store = KSPS_LOCAL;
  else if (g_strcmp0(persist_store, "remote") == 0)
    self->options.persist_store = KSPS_REMOTE;
  else
    return FALSE;
  return TRUE;
}

void
kafka_sd_set_poll_timeout(LogDriver *d, gint poll_timeout)
{
  KafkaSourceDriver *self = (KafkaSourceDriver *)d;

  kafka_options_set_poll_timeout(&self->options.super, poll_timeout);
}

void
kafka_sd_set_state_update_timeout(LogDriver *d, gint state_update_timeout)
{
  KafkaSourceDriver *self = (KafkaSourceDriver *)d;

  kafka_options_set_state_update_timeout(&self->options.super, state_update_timeout);
}

void
kafka_sd_set_time_reopen(LogDriver *d, gint time_reopen)
{
  KafkaSourceDriver *self = (KafkaSourceDriver *)d;

  self->options.time_reopen = time_reopen;
}

void
kafka_sd_set_log_fetch_delay(LogDriver *s, guint new_value)
{
  KafkaSourceDriver *self = (KafkaSourceDriver *)s;
  self->options.fetch_delay = new_value;
}

void kafka_sd_set_log_fetch_retry_delay(LogDriver *s, guint new_value)
{
  KafkaSourceDriver *self = (KafkaSourceDriver *)s;
  self->options.fetch_retry_delay = new_value;
}

void
kafka_sd_set_log_fetch_limit(LogDriver *s, guint new_value)
{
  KafkaSourceDriver *self = (KafkaSourceDriver *)s;
  self->options.fetch_limit = new_value;
}

void
kafka_sd_set_log_fetch_queue_full_delay(LogDriver *s, guint new_value)
{
  KafkaSourceDriver *self = (KafkaSourceDriver *)s;
  self->options.fetch_queue_full_delay = new_value;
}

void kafka_sd_set_separate_worker_queues(LogDriver *s, gboolean new_value)
{
  KafkaSourceDriver *self = (KafkaSourceDriver *)s;
  self->options.separated_worker_queues = new_value;
}

void
kafka_sd_options_defaults(KafkaSourceOptions *self,
                          LogThreadedSourceWorkerOptions *worker_options)
{
  self->worker_options = worker_options;
  self->worker_options->super.stats_level = STATS_LEVEL0;
  self->worker_options->super.stats_source = stats_register_type("kafka");

  /* No additional format options now, so intentionally referencing only, and not using msg_format_options_copy */
  self->format_options = &self->worker_options->parse_options;

  kafka_options_defaults(&self->super);
  self->strategy_hint = KSCS_ASSIGN;

  self->persist_store = KSPS_LOCAL;
  self->separated_worker_queues = FALSE;
  self->fetch_queue_full_delay = 1000; /* fetch_queue_full_delay milliseconds = 1 second */
  self->fetch_delay = 1000; /* 1 second / fetch_delay = 1 millisecond */
  self->fetch_retry_delay = 10000; /* 1 second / fetch_retry_delay = 0.1 millisecond */
  self->fetch_limit = 10000;
  self->time_reopen = 60; /* time_reopen seconds */
}

void
kafka_sd_options_destroy(KafkaSourceOptions *self)
{
  kafka_property_list_free(self->requested_topics);

  kafka_options_destroy(&self->super);

  /* Just referenced, not copied */
  self->format_options = NULL;
  self->worker_options = NULL;
}
