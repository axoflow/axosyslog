/*
 * Copyright (c) 2023 László Várady <laszlo.varady93@gmail.com>
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

#ifndef HEALTHCHECK_H
#define HEALTHCHECK_H

#include "syslog-ng.h"

typedef struct _HealthCheck HealthCheck;

typedef struct _HealthCheckResult
{
  guint64 io_worker_latency;
  guint64 mainloop_io_worker_roundtrip_latency;
} HealthCheckResult;

typedef void(*HealthCheckCompletionCB)(HealthCheckResult, gpointer);

HealthCheck *healthcheck_new(void);
HealthCheck *healthcheck_ref(HealthCheck *self);
void healthcheck_unref(HealthCheck *self);

gboolean healthcheck_run(HealthCheck *self, HealthCheckCompletionCB completion, gpointer user_data);

#endif
