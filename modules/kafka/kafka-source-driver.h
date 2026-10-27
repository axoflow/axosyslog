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

#ifndef KAFKA_SOURCE_DRIVER_H_INCLUDED
#define KAFKA_SOURCE_DRIVER_H_INCLUDED

#include "logthrsource/logthrsourcedrv.h"
#include "msg-format.h"
#include "stats/aggregator/stats-aggregator.h"
#include "kafka-internal.h"

/* milliseconds; a freshly created client needs to connect, negotiate the API
 * versions and fetch the metadata before it can answer the first state check,
 * a shorter timeout cannot be met on a cold connection */
#define KAFKA_STATE_UPDATE_TIMEOUT_MIN 1000

typedef struct _KafkaSourceOptions KafkaSourceOptions;
typedef struct _KafkaSourceDriver KafkaSourceDriver;
struct _KafkaSourcePersist;

typedef enum _KafkaConnectedState
{
  KFS_CONNECTED,
  KFS_DISCONNECTED,

  KFS_UNKNOWN
} KafkaConnectedState;

typedef enum _KafkaSrcConsumerStrategy
{
  KSCS_ASSIGN,
  KSCS_SUBSCRIBE,

  KSCS_UNDEFINED
} KafkaSrcConsumerStrategy;

typedef enum _KafkaSrcPersistStore
{
  KSPS_LOCAL,
  KSPS_REMOTE,
} KafkaSrcPersistStore;

struct _KafkaSourceOptions
{
  KafkaOptions super;
  /* WARNING: multiple inheritance! */
  LogThreadedSourceWorkerOptions *worker_options;

  MsgFormatOptions *format_options;
  gboolean store_kafka_metadata;

  GList *requested_topics;
  KafkaSrcConsumerStrategy strategy_hint;
  gint time_reopen;
  gint state_update_timeout;

  gboolean ignore_saved_bookmarks;
  gboolean disable_bookmarks;
  KafkaSrcPersistStore persist_store;
  guint fetch_delay;
  guint fetch_retry_delay;
  guint fetch_limit; // TODO: use together with "queued.max.messages.kbytes", if 0 kafka's own setting is used automatically
  guint fetch_queue_full_delay;
  gboolean separated_worker_queues;
};

struct _KafkaSourceDriver
{
  LogThreadedSourceDriver super;

  KafkaSourceOptions options;

  /* connection state, protected by state_mutex */
  GMutex state_mutex;
  KafkaConnectedState state;
  gint last_error;

  rd_kafka_t *kafka;
  rd_kafka_queue_t *consumer_kafka_queue;
  rd_kafka_queue_t *main_kafka_queue;
  rd_kafka_topic_partition_list_t *assigned_partitions;

  gchar *group_id;
  GList *requested_topics;

  KafkaSrcConsumerStrategy strategy;
  GAsyncQueue **msg_queues;
  GCond *queue_conds;
  GMutex *queue_cond_mutexes;
  guint allocated_queue_num;
  gchar single_queue_name[64];

  GMutex partition_assignement_mutex;
  gboolean reassign_signaled;
  gboolean assignement_invalidated_signaled;

  GAtomicCounter running_thread_num;
  GAtomicCounter sleeping_thread_num;

  GHashTable *persists;
  gboolean all_persists_ready;
  const gchar *persist_name;

  const gchar *stat_persist_name;
  GHashTable *stats_topics;
  GHashTable *stats_workers;
  StatsAggregator *max_message_size;
  StatsAggregator *average_messages_size;
  StatsAggregator *CPS;

};

void kafka_sd_merge_config(LogDriver *d, GList *props);
gboolean kafka_sd_set_logging(LogDriver *d, const gchar *logging);
gboolean kafka_sd_set_topics(LogDriver *d, GList *topics);
gboolean kafka_sd_set_strategy_hint(LogDriver *d, const gchar *strategy_hint);
gboolean kafka_sd_set_persis_store(LogDriver *d, const gchar *strategy_hint);
void kafka_sd_set_bootstrap_servers(LogDriver *d, const gchar *bootstrap_servers);
void kafka_sd_set_log_fetch_delay(LogDriver *s, guint new_value);
void kafka_sd_set_log_fetch_retry_delay(LogDriver *s, guint new_value);
void kafka_sd_set_log_fetch_limit(LogDriver *s, guint new_value);
void kafka_sd_set_log_fetch_queue_full_delay(LogDriver *s, guint new_value);
void kafka_sd_set_poll_timeout(LogDriver *d, gint poll_timeout);
void kafka_sd_set_state_update_timeout(LogDriver *d, gint state_update_timeout);
void kafka_sd_set_time_reopen(LogDriver *d, gint time_reopen);
void kafka_sd_set_ignore_saved_bookmarks(LogDriver *s, gboolean new_value);
void kafka_sd_set_disable_bookmarks(LogDriver *s, gboolean new_value);
void kafka_sd_set_separate_worker_queues(LogDriver *s, gboolean new_value);
void kafka_sd_set_store_kafka_metadata(LogDriver *s, gboolean new_value);

LogDriver *kafka_sd_new(GlobalConfig *cfg);

void kafka_sd_options_defaults(KafkaSourceOptions *self,
                               LogThreadedSourceWorkerOptions *worker_options);
void kafka_sd_options_destroy(KafkaSourceOptions *self);

gboolean kafka_sd_reopen(LogDriver *s);
rd_kafka_resp_err_t kafka_sd_update_state(KafkaSourceDriver *self, gboolean lock);
void kafka_sd_final_flush(KafkaSourceDriver *self);

gboolean kafka_sd_using_queues(KafkaSourceDriver *self);
guint kafka_sd_used_queue_num(KafkaSourceDriver *self);
guint kafka_sd_worker_queues_len(KafkaSourceDriver *self);
GAsyncQueue *kafka_sd_worker_queue(KafkaSourceDriver *self, LogThreadedSourceWorker *worker);
void kafka_sd_wait_for_queue(KafkaSourceDriver *self, LogThreadedSourceWorker *worker);
void kafka_sd_signal_queue(KafkaSourceDriver *self, LogThreadedSourceWorker *worker);
void kafka_sd_signal_queue_ndx(KafkaSourceDriver *self, guint ndx);
void kafka_sd_signal_queues(KafkaSourceDriver *self);
gboolean kafka_sd_wait_for_queue_processors_to_sleep(KafkaSourceDriver *self, const gdouble iteration_sleep_time,
                                                     gboolean poll_kafka);
void kafka_sd_wait_for_queue_processors_to_exit(KafkaSourceDriver *self, const gdouble iteration_sleep_time);
void kafka_sd_drop_queued_messages(KafkaSourceDriver *self);
void kafka_sd_wakeup_kafka_queues(KafkaSourceDriver *self);
void kafka_sd_signal_reassign(KafkaSourceDriver *self);
gboolean kafka_sd_reassign_signaled(KafkaSourceDriver *self);
void kafka_sd_signal_assignement_invalidated(KafkaSourceDriver *self);
gboolean kafka_sd_assignement_invalidated_signaled(KafkaSourceDriver *self);
void kafka_sd_persist_add_msg_bookmark(KafkaSourceDriver *self,
                                       AckTracker *ack_tracker,
                                       const gchar *msg_topic_name,
                                       int32_t msg_partition,
                                       int64_t msg_offset);
gboolean kafka_sd_store_persist_offset(KafkaSourceDriver *self,
                                       struct _KafkaSourcePersist *persist,
                                       int64_t offset);
gboolean kafka_sd_parallel_processing(KafkaSourceDriver *self);
gboolean kafka_sd_persist_all_ready(KafkaSourceDriver *self);
gboolean kafka_sd_persist_is_ready(KafkaSourceDriver *self,
                                   const gchar *msg_topic_name,
                                   int32_t msg_partition);

void kafka_sd_update_msg_length_stats(KafkaSourceDriver *self, gsize len);
void kafka_sd_inc_msg_topic_stats(KafkaSourceDriver *self, const gchar *topic);
void kafka_sd_update_msg_worker_stats(KafkaSourceDriver *self, gint worker_ndx);

static inline gdouble
_mainloop_sleep_time(const gdouble delay)
{
  return delay > 0 ? 1.0 / delay : 0.0;
}

#endif
