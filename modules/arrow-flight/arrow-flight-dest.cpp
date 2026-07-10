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
#include "messages.h"
#include "compat/cpp-end.h"

using syslog_ng::arrow_flight::DestinationDriver;
using syslog_ng::arrow_flight::SchemaField;

struct _ArrowFlightDestDriver
{
  LogThreadedDestDriver super;
  DestinationDriver *cpp;
};


DestinationDriver::DestinationDriver(ArrowFlightDestDriver *s) : super(s)
{
  log_template_options_defaults(&this->template_options);
}

DestinationDriver::~DestinationDriver()
{
  log_template_unref(this->path_template);
  log_template_options_destroy(&this->template_options);
}

bool
DestinationDriver::init()
{
  if (this->schema_fields.empty())
    {
      msg_error("Error initializing arrow-flight destination, schema() must be set",
                log_pipe_location_tag(&this->super->super.super.super.super));
      return false;
    }

  std::vector<std::shared_ptr<arrow::Field>> fields;
  for (const auto &f : this->schema_fields)
    fields.push_back(arrow::field(f.name, f.type));
  this->arrow_schema = arrow::schema(fields);

  GlobalConfig *cfg = log_pipe_get_config(&this->super->super.super.super.super);
  log_template_options_init(&this->template_options, cfg);

  log_threaded_dest_driver_set_flush_on_worker_key_change(&this->super->super.super.super, TRUE);

  if (this->path_template &&
      !log_template_is_literal_string(this->path_template) &&
      !this->super->super.worker_partition_key)
    {
      log_threaded_dest_driver_set_worker_partition_key_ref(&this->super->super.super.super,
                                                            log_template_ref(this->path_template));
    }

  if (!log_threaded_dest_driver_init_method(&this->super->super.super.super.super))
    return false;

  if (this->batch_bytes > 0 && this->super->super.batch_lines <= 0)
    this->super->super.batch_lines = G_MAXINT;

  return true;
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
    g_snprintf(persist_name, sizeof(persist_name), "arrow-flight(%s,%s)",
               this->url.c_str(), this->path_template ? this->path_template->template_str : "");

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

void
arrow_flight_dd_set_path(LogDriver *d, LogTemplate *path)
{
  ArrowFlightDestDriver *self = (ArrowFlightDestDriver *) d;
  self->cpp->set_path_template(path);
}

void
arrow_flight_dd_set_batch_bytes(LogDriver *d, glong b)
{
  ArrowFlightDestDriver *self = (ArrowFlightDestDriver *) d;
  self->cpp->set_batch_bytes((size_t) b);
}

void
arrow_flight_dd_set_keepalive_time(LogDriver *d, gint t)
{
  ArrowFlightDestDriver *self = (ArrowFlightDestDriver *) d;
  self->cpp->set_keepalive_time(t);
}

void
arrow_flight_dd_set_keepalive_timeout(LogDriver *d, gint t)
{
  ArrowFlightDestDriver *self = (ArrowFlightDestDriver *) d;
  self->cpp->set_keepalive_timeout(t);
}

void
arrow_flight_dd_set_keepalive_max_pings(LogDriver *d, gint p)
{
  ArrowFlightDestDriver *self = (ArrowFlightDestDriver *) d;
  self->cpp->set_keepalive_max_pings(p);
}

void
arrow_flight_dd_add_string_schema_field(LogDriver *d, const gchar *name, LogTemplate *value)
{
  ArrowFlightDestDriver *self = (ArrowFlightDestDriver *) d;
  self->cpp->add_string_schema_field(name, value);
}

void
arrow_flight_dd_add_integer_schema_field(LogDriver *d, const gchar *name, LogTemplate *value)
{
  ArrowFlightDestDriver *self = (ArrowFlightDestDriver *) d;
  self->cpp->add_integer_schema_field(name, value);
}

void
arrow_flight_dd_add_double_schema_field(LogDriver *d, const gchar *name, LogTemplate *value)
{
  ArrowFlightDestDriver *self = (ArrowFlightDestDriver *) d;
  self->cpp->add_double_schema_field(name, value);
}

void
arrow_flight_dd_add_bool_schema_field(LogDriver *d, const gchar *name, LogTemplate *value)
{
  ArrowFlightDestDriver *self = (ArrowFlightDestDriver *) d;
  self->cpp->add_bool_schema_field(name, value);
}

void
arrow_flight_dd_add_timestamp_schema_field(LogDriver *d, const gchar *name, LogTemplate *value)
{
  ArrowFlightDestDriver *self = (ArrowFlightDestDriver *) d;
  self->cpp->add_timestamp_schema_field(name, value);
}

void
arrow_flight_dd_add_map_string_string_schema_field(LogDriver *d, const gchar *name, LogTemplate *value)
{
  ArrowFlightDestDriver *self = (ArrowFlightDestDriver *) d;
  self->cpp->add_map_string_string_schema_field(name, value);
}

LogTemplateOptions *
arrow_flight_dd_get_template_options(LogDriver *d)
{
  ArrowFlightDestDriver *self = (ArrowFlightDestDriver *) d;
  return &self->cpp->get_template_options();
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
