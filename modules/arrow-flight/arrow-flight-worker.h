/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Attila Szakacs <attila.szakacs@axoflow.com>
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

#ifndef ARROW_FLIGHT_WORKER_H
#define ARROW_FLIGHT_WORKER_H

#include "syslog-ng.h"

#include "compat/cpp-start.h"
#include "logthrdest/logthrdestdrv.h"

typedef struct _ArrowFlightDestWorker ArrowFlightDestWorker;

LogThreadedDestWorker *arrow_flight_dw_new(LogThreadedDestDriver *o, gint worker_index);

#include "compat/cpp-end.h"

#endif
