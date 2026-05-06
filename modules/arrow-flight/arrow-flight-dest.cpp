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

#include "arrow-flight-dest.hpp"
#include "arrow-flight-worker.hpp"

#include "compat/cpp-start.h"
#include "logthrdest/logthrdestdrv.h"
#include "stats/stats-cluster-key-builder.h"
#include "compat/cpp-end.h"

using syslog_ng::arrow_flight::DestinationDriver;

struct _ArrowFlightDestDriver
{
  LogThreadedDestDriver super;
  DestinationDriver *cpp;
};


DestinationDriver::DestinationDriver(ArrowFlightDestDriver *s) : super(s)
{
}

DestinationDriver::~DestinationDriver()
{
}

bool
DestinationDriver::init()
{
  return log_threaded_dest_driver_init_method(&this->super->super.super.super.super);
}

bool
DestinationDriver::deinit()
{
  return log_threaded_dest_driver_deinit_method(&this->super->super.super.super.super);
}

const gchar *
DestinationDriver::format_persist_name()
{
  static gchar persist_name[1024];

  LogPipe *s = &this->super->super.super.super.super;
  if (s->persist_name)
    g_snprintf(persist_name, sizeof(persist_name), "arrow-flight.%s", s->persist_name);
  else
    g_snprintf(persist_name, sizeof(persist_name), "arrow-flight(%s)", this->url.c_str());

  return persist_name;
}

/* C Wrappers */

syslog_ng::arrow_flight::DestinationDriver *
arrow_flight_dd_get_cpp(LogDriver *d)
{
  ArrowFlightDestDriver *self = (ArrowFlightDestDriver *) d;
  return self->cpp;
}

static const gchar *
_format_persist_name(const LogPipe *s)
{
  ArrowFlightDestDriver *self = (ArrowFlightDestDriver *) s;
  return self->cpp->format_persist_name();
}

static const gchar *
_format_stats_key(LogThreadedDestDriver *s, StatsClusterKeyBuilder *kb)
{
  ArrowFlightDestDriver *self = (ArrowFlightDestDriver *) s;

  stats_cluster_key_builder_add_legacy_label(kb, stats_cluster_label("driver", "arrow-flight"));
  stats_cluster_key_builder_add_legacy_label(kb, stats_cluster_label("url", self->cpp->get_url().c_str()));

  return NULL;
}

void
arrow_flight_dd_set_url(LogDriver *d, const gchar *url)
{
  ArrowFlightDestDriver *self = (ArrowFlightDestDriver *) d;
  self->cpp->set_url(url);
}

static gboolean
_init(LogPipe *s)
{
  ArrowFlightDestDriver *self = (ArrowFlightDestDriver *) s;
  return self->cpp->init();
}

static gboolean
_deinit(LogPipe *s)
{
  ArrowFlightDestDriver *self = (ArrowFlightDestDriver *) s;
  return self->cpp->deinit();
}

static void
_free(LogPipe *s)
{
  ArrowFlightDestDriver *self = (ArrowFlightDestDriver *) s;
  delete self->cpp;

  log_threaded_dest_driver_free(s);
}

LogDriver *
arrow_flight_dd_new(GlobalConfig *cfg)
{
  ArrowFlightDestDriver *self = g_new0(ArrowFlightDestDriver, 1);

  log_threaded_dest_driver_init_instance(&self->super, cfg);

  self->cpp = new DestinationDriver(self);

  self->super.super.super.super.init = _init;
  self->super.super.super.super.deinit = _deinit;
  self->super.super.super.super.free_fn = _free;
  self->super.super.super.super.generate_persist_name = _format_persist_name;

  self->super.format_stats_key = _format_stats_key;
  self->super.stats_source = stats_register_type("arrow-flight");

  self->super.worker.construct = arrow_flight_dw_new;

  return &self->super.super.super;
}
