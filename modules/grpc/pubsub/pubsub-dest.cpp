/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 Attila Szakacs <attila.szakacs@axoflow.com>
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

#include "pubsub-dest.hpp"
#include "pubsub-dest-worker.hpp"
#include "grpc-dest-worker.hpp"

#include "compat/cpp-start.h"
#include "messages.h"
#include "compat/cpp-end.h"

#include <string.h>

using syslogng::grpc::pubsub::DestDriver;

const size_t MAX_BATCH_BYTES = 10 * 1000 * 1000;

DestDriver::DestDriver(GrpcDestDriver *s)
  : syslogng::grpc::DestDriver(s)
{
  this->url = "pubsub.googleapis.com:443";
  this->credentials_builder.set_mode(GCAM_ADC);
  this->enable_dynamic_headers();

  /* https://cloud.google.com/pubsub/quotas#resource_limits */
  this->batch_bytes = MAX_BATCH_BYTES;

  GlobalConfig *cfg = log_pipe_get_config(&s->super.super.super.super);
  this->default_data_template = log_template_new(cfg, NULL);
  g_assert(log_template_compile(this->default_data_template, "$MESSAGE", NULL));
  this->set_data(this->default_data_template);
}

DestDriver::~DestDriver()
{
  log_template_unref(this->project);
  log_template_unref(this->topic);
  log_template_unref(this->data);
  log_template_unref(this->default_data_template);
}

bool
DestDriver::init()
{
  if (this->batch_bytes > MAX_BATCH_BYTES)
    {
      msg_error("Error initializing Google Pub/Sub destination, batch-bytes() cannot be larger than 10 MB. "
                "For more info see https://cloud.google.com/pubsub/quotas#resource_limits",
                log_pipe_location_tag(&this->super->super.super.super.super));
      return false;
    }

  if ((!this->project || strlen(this->project->template_str) == 0) ||
      (!this->topic || strlen(this->topic->template_str) == 0))
    {
      msg_error("Error initializing Google Pub/Sub destination, project() and topic() are mandatory options",
                log_pipe_location_tag(&this->super->super.super.super.super));
      return false;
    }
  if ((!this->attributes.empty() || this->data != this->default_data_template) && this->proto_var != nullptr)
    {
      msg_error("Error initializing Google Pub/Sub destination: 'attributes()' and 'data()' cannot be used together with 'proto_var()'. Please use either 'attributes()' and 'data()', or 'proto_var()', but not both.",
                log_pipe_location_tag(&this->super->super.super.super.super));
      return false;
    }

  this->extend_worker_partition_key(std::string("project=") + this->project->template_str);
  this->extend_worker_partition_key(std::string("topic=") + this->topic->template_str);

  return syslogng::grpc::DestDriver::init();
}

const gchar *
DestDriver::generate_persist_name()
{
  static gchar persist_name[1024];

  LogPipe *s = &this->super->super.super.super.super;
  if (s->persist_name)
    g_snprintf(persist_name, sizeof(persist_name), "google_pubsub_grpc.%s", s->persist_name);
  else
    g_snprintf(persist_name, sizeof(persist_name), "google_pubsub_grpc(%s,%s,%s)",
               this->url.c_str(), this->project->template_str, this->topic->template_str);

  return persist_name;
}

const gchar *
DestDriver::format_stats_key(StatsClusterKeyBuilder *kb)
{
  stats_cluster_key_builder_add_legacy_label(kb, stats_cluster_label("driver", "pubsub"));
  stats_cluster_key_builder_add_legacy_label(kb, stats_cluster_label("url", this->url.c_str()));
  stats_cluster_key_builder_add_legacy_label(kb, stats_cluster_label("project", this->project->template_str));
  stats_cluster_key_builder_add_legacy_label(kb, stats_cluster_label("topic", this->topic->template_str));

  return nullptr;
}

LogThreadedDestWorker *
DestDriver::construct_worker(int worker_index)
{
  GrpcDestWorker *worker = grpc_dw_new(this->super, worker_index);
  worker->cpp = new DestWorker(worker);
  return &worker->super;
}


/* C Wrappers */

DestDriver *
pubsub_dd_get_cpp(GrpcDestDriver *self)
{
  return (DestDriver *) self->cpp;
}

void
pubsub_dd_set_project(LogDriver *d, LogTemplate *project)
{
  GrpcDestDriver *self = (GrpcDestDriver *) d;
  DestDriver *cpp = pubsub_dd_get_cpp(self);
  cpp->set_project(project);
}

void
pubsub_dd_set_topic(LogDriver *d, LogTemplate *topic)
{
  GrpcDestDriver *self = (GrpcDestDriver *) d;
  DestDriver *cpp = pubsub_dd_get_cpp(self);
  cpp->set_topic(topic);
}

void
pubsub_dd_set_data(LogDriver *d, LogTemplate *data)
{
  GrpcDestDriver *self = (GrpcDestDriver *) d;
  DestDriver *cpp = pubsub_dd_get_cpp(self);
  cpp->set_data(data);
}

void
pubsub_dd_add_attribute(LogDriver *d, const gchar *name, LogTemplate *value)
{
  GrpcDestDriver *self = (GrpcDestDriver *) d;
  DestDriver *cpp = pubsub_dd_get_cpp(self);
  cpp->add_attribute(name, value);
}

LogDriver *
pubsub_dd_new(GlobalConfig *cfg)
{
  GrpcDestDriver *self = grpc_dd_new(cfg, "google_pubsub_grpc");
  self->cpp = new DestDriver(self);
  return &self->super.super.super;
}
