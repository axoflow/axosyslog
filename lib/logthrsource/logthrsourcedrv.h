/*
 * Copyright (c) 2018 Balabit
 * Copyright (c) 2018 László Várady <laszlo.varady@balabit.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#ifndef LOGTHRSOURCEDRV_H
#define LOGTHRSOURCEDRV_H

#include "syslog-ng.h"
#include "driver.h"
#include "logsource.h"
#include "cfg.h"
#include "logpipe.h"
#include "logmsg/logmsg.h"
#include "msg-format.h"
#include "mainloop-threaded-worker.h"
#include "stats/stats-cluster-key-builder.h"

typedef struct _LogThreadedSourceDriver LogThreadedSourceDriver;
typedef struct _LogThreadedSourceWorker LogThreadedSourceWorker;

typedef struct _LogThreadedSourceWorkerOptions
{
  LogSourceOptions super;
  MsgFormatOptions parse_options;
  AckTrackerFactory *ack_tracker_factory;
} LogThreadedSourceWorkerOptions;

typedef struct _WakeupCondition
{
  GMutex lock;
  GCond cond;
  gboolean awoken;
} WakeupCondition;

struct _LogThreadedSourceWorker
{
  LogSource super;
  MainLoopThreadedWorker thread;
  LogThreadedSourceDriver *control;
  WakeupCondition wakeup_cond;
  gboolean under_termination;
  gint worker_index;

  gboolean (*thread_init)(LogThreadedSourceWorker *self);
  void (*thread_deinit)(LogThreadedSourceWorker *self);
  void (*run)(LogThreadedSourceWorker *self);
  void (*request_exit)(LogThreadedSourceWorker *self);
  void (*wakeup)(LogThreadedSourceWorker *self);
};

struct _LogThreadedSourceDriver
{
  LogSrcDriver super;
  LogThreadedSourceWorkerOptions worker_options;
  LogThreadedSourceWorker **workers;
  gint num_workers;
  gboolean auto_close_batches;
  gchar *transport_name;
  gsize transport_name_len;

  void (*format_stats_key)(LogThreadedSourceDriver *self, StatsClusterKeyBuilder *kb);
  LogThreadedSourceWorker *(*worker_construct)(LogThreadedSourceDriver *self, gint worker_index);
};

void log_threaded_source_worker_options_defaults(LogThreadedSourceWorkerOptions *options);
void log_threaded_source_worker_options_init(LogThreadedSourceWorkerOptions *options, GlobalConfig *cfg,
                                             const gchar *group_name, gint num_workers);
void log_threaded_source_worker_options_destroy(LogThreadedSourceWorkerOptions *options);

void log_threaded_source_driver_set_transport_name(LogThreadedSourceDriver *self, const gchar *transport_name);
void log_threaded_source_driver_init_instance(LogThreadedSourceDriver *self, GlobalConfig *cfg);
gboolean log_threaded_source_driver_init_method(LogPipe *s);
gboolean log_threaded_source_driver_deinit_method(LogPipe *s);
gboolean log_threaded_source_driver_pre_config_init_method(LogPipe *s);
gboolean log_threaded_source_driver_post_config_init_method(LogPipe *s);
void log_threaded_source_driver_free_method(LogPipe *s);

static inline void
log_threaded_source_driver_set_num_workers(LogDriver *s, gint num_workers)
{
  LogThreadedSourceDriver *self = (LogThreadedSourceDriver *) s;
  self->num_workers = num_workers;
}

static inline LogSourceOptions *
log_threaded_source_driver_get_source_options(LogDriver *s)
{
  LogThreadedSourceDriver *self = (LogThreadedSourceDriver *) s;

  return &self->worker_options.super;
}

static inline MsgFormatOptions *
log_threaded_source_driver_get_parse_options(LogDriver *s)
{
  LogThreadedSourceDriver *self = (LogThreadedSourceDriver *) s;

  return &self->worker_options.parse_options;
}

/* Worker */

void log_threaded_source_worker_init_instance(LogThreadedSourceWorker *self, LogThreadedSourceDriver *driver,
                                              gint worker_index);
void log_threaded_source_worker_free(LogPipe *s);

void log_threaded_source_worker_close_batch(LogThreadedSourceWorker *self);

/* blocking API */
void log_threaded_source_worker_blocking_post(LogThreadedSourceWorker *self, LogMessage *msg);

/* non-blocking API, use it wisely (thread boundaries); call close_batch() at least before suspending */
void log_threaded_source_worker_post(LogThreadedSourceWorker *self, LogMessage *msg);
gboolean log_threaded_source_worker_free_to_send(LogThreadedSourceWorker *self);

#endif
