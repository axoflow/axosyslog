/*
 * Copyright (c) 2002-2013 Balabit
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
#ifndef CONTROL_SERVER_H
#define CONTROL_SERVER_H

#include "syslog-ng.h"
#include "control.h"
#include "atomic.h"
#include <stdio.h>

#define MAX_CONTROL_LINE_LENGTH 4096

struct _ControlServer
{
  GList *worker_threads;
  gboolean (*start)(ControlServer *s);
  void (*stop)(ControlServer *s);
  void (*free_fn)(ControlServer *self);
};

void control_server_cancel_all_workers(ControlServer *self);
void control_server_connection_closed(ControlServer *self, ControlConnection *cc);
void control_server_worker_started(ControlServer *self, ControlCommandThread *worker);
void control_server_worker_finished(ControlServer *self, ControlCommandThread *worker);

gboolean control_server_start_method(ControlServer *self);
void control_server_stop_method(ControlServer *self);
void control_server_free_method(ControlServer *self);
void control_server_free(ControlServer *self);
void control_server_init_instance(ControlServer *self);

static inline gboolean
control_server_start(ControlServer *self)
{
  if (self->start)
    return self->start(self);
  return TRUE;
}

static inline void
control_server_stop(ControlServer *self)
{
  if (self->stop)
    self->stop(self);
}

#endif
