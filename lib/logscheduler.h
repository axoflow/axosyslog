/*
 * Copyright (c) 2022 Balázs Scheidler <bazsi77@gmail.com>
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

#ifndef LOGSCHEDULER_H_INCLUDED
#define LOGSCHEDULER_H_INCLUDED

#include "logpipe.h"
#include "mainloop-io-worker.h"
#include "template/templates.h"
#include "stats/stats-counter.h"

#include <iv_list.h>
#include <iv_event.h>

#define LOGSCHEDULER_MAX_PARTITIONS 32

typedef struct _LogSchedulerBatch
{
  struct iv_list_head elements;
  struct iv_list_head list;
} LogSchedulerBatch;

typedef struct _LogSchedulerPartition
{
  GMutex batches_lock;
  struct iv_list_head batches;
  gboolean flush_running;
  MainLoopIOWorkerJob io_job;
  LogPipe *front_pipe;
  struct {
    StatsClusterKey assigned_events_total_key;
    StatsClusterKey processed_events_total_key;
    StatsCounterItem *assigned_events_total;
    StatsCounterItem *processed_events_total;
  } metrics;
} LogSchedulerPartition;

typedef struct _LogSchedulerThreadState
{
  WorkerBatchCallback batch_callback;
  guint8 batch_callback_registered:1;
  struct iv_list_head batch_by_partition[LOGSCHEDULER_MAX_PARTITIONS];

  gint last_partition;
  gint current_batch_size;
} LogSchedulerThreadState;

typedef struct _LogSchedulerOptions
{
  gint num_partitions;
  gint batch_size;
  LogTemplate *partition_key;
} LogSchedulerOptions;

typedef struct _LogScheduler
{
  gchar *id;
  LogPipe *front_pipe;
  LogSchedulerOptions *options;
  gint num_input_threads;
  LogSchedulerPartition partitions[LOGSCHEDULER_MAX_PARTITIONS];
  LogSchedulerThreadState input_thread_states[];
} LogScheduler;

gboolean log_scheduler_init(LogScheduler *self);
void log_scheduler_deinit(LogScheduler *self);

void log_scheduler_push(LogScheduler *self, LogMessage *msg, const LogPathOptions *path_options);
LogScheduler *log_scheduler_new(LogSchedulerOptions *options, LogPipe *front_pipe, const gchar *id);
void log_scheduler_free(LogScheduler *self);

void log_scheduler_options_set_partition_key_ref(LogSchedulerOptions *options, LogTemplate *partition_key);
void log_scheduler_options_defaults(LogSchedulerOptions *options);
gboolean log_scheduler_options_init(LogSchedulerOptions *options, GlobalConfig *cfg);
void log_scheduler_options_destroy(LogSchedulerOptions *options);

#endif
