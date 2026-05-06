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

#include "arrow-flight-worker.hpp"

#include "compat/cpp-start.h"
#include "logthrdest/logthrdestdrv.h"
#include "compat/cpp-end.h"

using syslog_ng::arrow_flight::DestinationDriver;
using syslog_ng::arrow_flight::DestinationWorker;

struct _ArrowFlightDestWorker
{
  LogThreadedDestWorker super;
  DestinationWorker *cpp;
};


DestinationWorker::DestinationWorker(ArrowFlightDestWorker *s) : super(s)
{
}

DestinationWorker::~DestinationWorker()
{
}

DestinationDriver *
DestinationWorker::get_owner()
{
  return arrow_flight_dd_get_cpp(&this->super->super.owner->super.super);
}

bool
DestinationWorker::init()
{
  return log_threaded_dest_worker_init_method(&this->super->super);
}

void
DestinationWorker::deinit()
{
  log_threaded_dest_worker_deinit_method(&this->super->super);
}

bool
DestinationWorker::connect()
{
  return true;
}

void
DestinationWorker::disconnect()
{
}

LogThreadedResult
DestinationWorker::insert(LogMessage *msg)
{
  return LTR_QUEUED;
}

LogThreadedResult
DestinationWorker::flush(LogThreadedFlushMode mode)
{
  return LTR_SUCCESS;
}


/* C Wrappers */

static LogThreadedResult
_insert(LogThreadedDestWorker *s, LogMessage *msg)
{
  ArrowFlightDestWorker *self = (ArrowFlightDestWorker *) s;
  return self->cpp->insert(msg);
}

static LogThreadedResult
_flush(LogThreadedDestWorker *s, LogThreadedFlushMode mode)
{
  ArrowFlightDestWorker *self = (ArrowFlightDestWorker *) s;
  return self->cpp->flush(mode);
}

static gboolean
_init(LogThreadedDestWorker *s)
{
  ArrowFlightDestWorker *self = (ArrowFlightDestWorker *) s;
  return self->cpp->init();
}

static void
_deinit(LogThreadedDestWorker *s)
{
  ArrowFlightDestWorker *self = (ArrowFlightDestWorker *) s;
  self->cpp->deinit();
}

static gboolean
_connect(LogThreadedDestWorker *s)
{
  ArrowFlightDestWorker *self = (ArrowFlightDestWorker *) s;
  return self->cpp->connect();
}

static void
_disconnect(LogThreadedDestWorker *s)
{
  ArrowFlightDestWorker *self = (ArrowFlightDestWorker *) s;
  self->cpp->disconnect();
}

static void
_free(LogThreadedDestWorker *s)
{
  ArrowFlightDestWorker *self = (ArrowFlightDestWorker *) s;
  delete self->cpp;

  log_threaded_dest_worker_free_method(s);
}

LogThreadedDestWorker *
arrow_flight_dw_new(LogThreadedDestDriver *o, gint worker_index)
{
  ArrowFlightDestWorker *self = g_new0(ArrowFlightDestWorker, 1);

  log_threaded_dest_worker_init_instance(&self->super, o, worker_index);

  self->cpp = new DestinationWorker(self);

  self->super.init = _init;
  self->super.deinit = _deinit;
  self->super.connect = _connect;
  self->super.disconnect = _disconnect;
  self->super.insert = _insert;
  self->super.flush = _flush;
  self->super.free_fn = _free;

  return &self->super;
}
