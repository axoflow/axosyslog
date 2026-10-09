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

#include "kafka-source-driver.h"
#include "kafka-internal.h"

static void
_format_stats_key(LogThreadedSourceDriver *d, StatsClusterKeyBuilder *kb)
{
  KafkaSourceDriver *self = (KafkaSourceDriver *)d;

  stats_cluster_key_builder_add_legacy_label(kb, stats_cluster_label("driver", "kafka"));
  if (self->group_id)
    stats_cluster_key_builder_add_legacy_label(kb, stats_cluster_label("group_id", self->group_id));
}

static const gchar *
_format_persist_name(const LogPipe *d)
{
  const KafkaSourceDriver *self = (const KafkaSourceDriver *)d;
  static gchar persist_name[1024];

  if (d->persist_name)
    g_snprintf(persist_name, sizeof(persist_name), "kafka.%s", d->persist_name);
  else
    g_snprintf(persist_name, sizeof(persist_name), "kafka(%s)", self->super.super.super.id);
  return persist_name;
}

static gboolean
kafka_sd_init(LogPipe *s)
{
  KafkaSourceDriver *self = (KafkaSourceDriver *)s;

  if (self->options.super.bootstrap_servers == NULL)
    {
      msg_error("kafka: the bootstrap-servers() option is required for kafka source",
                evt_tag_str("driver", self->super.super.super.id),
                log_pipe_location_tag(&self->super.super.super.super));
      return FALSE;
    }

  kafka_opaque_init(&self->opaque, &self->super.super.super, &self->options.super);

  if (FALSE == log_threaded_source_driver_init_method(s))
    return FALSE;

  msg_verbose("kafka: Kafka source initialized",
              evt_tag_str("group_id", self->group_id),
              evt_tag_str("driver", self->super.super.super.id),
              log_pipe_location_tag(&self->super.super.super.super));
  return TRUE;
}

static gboolean
kafka_sd_deinit(LogPipe *s)
{
  KafkaSourceDriver *self = (KafkaSourceDriver *)s;

  kafka_opaque_deinit(&self->opaque);

  return log_threaded_source_driver_deinit_method(s);
}

static void
kafka_sd_free(LogPipe *s)
{
  KafkaSourceDriver *self = (KafkaSourceDriver *) s;

  kafka_sd_options_destroy(&self->options);

  log_threaded_source_driver_free_method(s);
}

LogDriver *
kafka_sd_new(GlobalConfig *cfg)
{
  KafkaSourceDriver *self = g_new0(KafkaSourceDriver, 1);
  log_threaded_source_driver_init_instance(&self->super, cfg);

  self->super.super.super.super.init = kafka_sd_init;
  self->super.super.super.super.deinit = kafka_sd_deinit;
  self->super.super.super.super.free_fn = kafka_sd_free;
  self->super.super.super.super.generate_persist_name = _format_persist_name;

  self->super.format_stats_key = _format_stats_key;

  kafka_sd_options_defaults(&self->options, &self->super.worker_options);

  return &self->super.super.super;
}

/* *********
 *  Options
 * *********/

void
kafka_sd_merge_config(LogDriver *d, GList *props)
{
  KafkaSourceDriver *self = (KafkaSourceDriver *)d;

  kafka_options_merge_config(&self->options.super, props);
}

gboolean
kafka_sd_set_logging(LogDriver *d, const gchar *logging)
{
  KafkaSourceDriver *self = (KafkaSourceDriver *)d;

  return kafka_options_set_logging(&self->options.super, logging);
}

void
kafka_sd_set_bootstrap_servers(LogDriver *d, const gchar *bootstrap_servers)
{
  KafkaSourceDriver *self = (KafkaSourceDriver *)d;

  kafka_options_set_bootstrap_servers(&self->options.super, bootstrap_servers);
}

void
kafka_sd_set_poll_timeout(LogDriver *d, gint poll_timeout)
{
  KafkaSourceDriver *self = (KafkaSourceDriver *)d;

  kafka_options_set_poll_timeout(&self->options.super, poll_timeout);
}

void
kafka_sd_set_state_update_timeout(LogDriver *d, gint state_update_timeout)
{
  KafkaSourceDriver *self = (KafkaSourceDriver *)d;

  kafka_options_set_state_update_timeout(&self->options.super, state_update_timeout);
}

void
kafka_sd_set_time_reopen(LogDriver *d, gint time_reopen)
{
  KafkaSourceDriver *self = (KafkaSourceDriver *)d;

  self->options.time_reopen = time_reopen;
}

void
kafka_sd_options_defaults(KafkaSourceOptions *self,
                          LogThreadedSourceWorkerOptions *worker_options)
{
  self->worker_options = worker_options;
  self->worker_options->super.stats_level = STATS_LEVEL0;
  self->worker_options->super.stats_source = stats_register_type("kafka");

  /* No additional format options now, so intentionally referencing only, and not using msg_format_options_copy */
  self->format_options = &self->worker_options->parse_options;

  kafka_options_defaults(&self->super);

  self->time_reopen = 60; /* time_reopen seconds */
}

void
kafka_sd_options_destroy(KafkaSourceOptions *self)
{
  kafka_options_destroy(&self->super);

  /* Just referenced, not copied */
  self->format_options = NULL;
  self->worker_options = NULL;
}
