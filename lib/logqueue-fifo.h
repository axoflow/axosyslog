/*
 * Copyright (c) 2002-2011 Balabit
 * Copyright (c) 1998-2011 Balázs Scheidler
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

#ifndef LOGQUEUE_FIFO_H_INCLUDED
#define LOGQUEUE_FIFO_H_INCLUDED

#include "logqueue.h"

LogQueue *log_queue_fifo_new(gint log_fifo_size, const gchar *persist_name, gint stats_level,
                             StatsClusterKeyBuilder *driver_sck_builder,
                             StatsClusterKeyBuilder *queue_sck_builder);

QueueType log_queue_fifo_get_type(void);

#endif
