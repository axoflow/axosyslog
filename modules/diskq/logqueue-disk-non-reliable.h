/*
 * Copyright (c) 2002-2016 Balabit
 * Copyright (c) 2016 Viktor Juhasz <viktor.juhasz@balabit.com>
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

#ifndef LOG_QUEUE_DISK_NON_RELIABLE_H_
#define LOG_QUEUE_DISK_NON_RELIABLE_H_

#include "logqueue-disk.h"

typedef struct _LogQueueDiskNonReliable
{
  LogQueueDisk super;
  GQueue *front_cache;
  GQueue *flow_control_window;
  GQueue *backlog;
  gint front_cache_size;
} LogQueueDiskNonReliable;

LogQueue *log_queue_disk_non_reliable_new(DiskQueueOptions *options, const gchar *filename, const gchar *persist_name,
                                          gint stats_level, StatsClusterKeyBuilder *driver_sck_builder,
                                          StatsClusterKeyBuilder *queue_sck_builder);

#endif /* LOG_QUEUE_DISK_NON_RELIABLE_H_ */
