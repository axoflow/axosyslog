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

#include "kafka-source-worker.h"
#include "kafka-internal.h"

static gboolean
_kafka_src_worker_init(LogThreadedSourceWorker *worker,
                       /* cannot use worker->control, not yet set */
                       LogThreadedSourceDriver *owner)
{
  KafkaSourceWorker *self = (KafkaSourceWorker *) worker;

  int ret = g_snprintf(self->name, sizeof(self->name), "worker#%d", self->super.worker_index);
  g_assert((gsize)ret < sizeof(self->name));

  return TRUE;
}

inline const gchar *
kafka_src_worker_get_name(LogThreadedSourceWorker *worker)
{
  KafkaSourceWorker *self = (KafkaSourceWorker *) worker;
  return self->name;
}

LogThreadedSourceWorker *kafka_src_worker_new(LogThreadedSourceDriver *owner, gint worker_index)
{
  KafkaSourceWorker *self = g_new0(KafkaSourceWorker, 1);

  log_threaded_source_worker_init_instance(&self->super, owner, worker_index);

  /* NOTE: Cannot use self->super.thread_init, as kafka_src_worker_get_name might be called before thread_init is called
   *       also, name cannot be a dynamically allocated GString, as it might be used during shutdown/cleanups etc.,
   *       like the worker_index, and currently there is no LogThreadedSourceDriver thread.free hook to free such resources.
   *       It also means that the control pointer still not set when thread_init is called, we must pass the owner explicitly.
   */
  _kafka_src_worker_init(&self->super, owner);

  return &self->super;
}
