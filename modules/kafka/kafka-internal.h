/*
 * Copyright (c) 2020 Balabit
 * Copyright (c) 2020 Balazs Scheidler
 * Copyright (c) 2020 Vivin Peris
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
 * Original Kafka source implementation sourced from https://github.com/syslog-ng/syslog-ng/pull/5564.
 * The license has been upgraded from GPL-2.0-or-later to GPL-3.0-or-later.
 */

#ifndef KAFKA_INTERNAL_H_INCLUDED
#define KAFKA_INTERNAL_H_INCLUDED

#include "syslog-ng.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#include <librdkafka/rdkafka.h>
#pragma GCC diagnostic pop

#define KAFKA_DEEP_TRACE 0
#if SYSLOG_NG_ENABLE_DEBUG
# define kafka_msg_debug msg_verbose
# define kafka_msg_trace msg_verbose
# if KAFKA_DEEP_TRACE
#  define kafka_msg_deep_trace msg_verbose
# else
#  define kafka_msg_deep_trace(msg, ...)
# endif
#else
# define kafka_msg_debug msg_debug
# define kafka_msg_trace msg_trace
# define kafka_msg_deep_trace(msg, ...)
#endif

#define MAX_KAFKA_TOPIC_NAME_LEN 249 // TODO: get from librdkafka?
#define MAX_KAFKA_PARTITION_KEY_NAME_LEN (MAX_KAFKA_TOPIC_NAME_LEN + 1 + 32) /* extra for partition number */

#define TOPIC_NAME_ERROR topic_name_error_quark()

typedef enum _KafkaLogging
{
  KFL_DISABLED,
  KFL_KAFKA_LEVEL,
  KFL_TRACE_LEVEL,

  KFL_UNKNOWN
} KafkaLogging;

typedef enum _KafkaTopicError
{
  TOPIC_LENGTH_ZERO,
  TOPIC_DOT_TWO_DOTS,
  TOPIC_EXCEEDS_MAX_LENGTH,
  TOPIC_INVALID_PATTERN,
} KafkaTopicError;

GQuark topic_name_error_quark(void);

gboolean kafka_validate_topic_pattern(const char *topic, GError **error);
gboolean kafka_validate_topic_name(const gchar *name, GError **error);
gboolean kafka_conf_set_prop(rd_kafka_conf_t *conf, const gchar *name, const gchar *value);
gboolean kafka_apply_config_props(rd_kafka_conf_t *conf, GList *props, gchar **protected_properties,
                                  gsize protected_properties_num);
void kafka_log_message(KafkaLogging logging, int level, const char *fac, const char *msg);

typedef struct _KafkaOptions
{
  gchar *bootstrap_servers;
  GList *config;
  KafkaLogging kafka_logging;
  gint poll_timeout;
} KafkaOptions;

void kafka_options_defaults(KafkaOptions *self);
void kafka_options_destroy(KafkaOptions *self);
void kafka_options_merge_config(KafkaOptions *self, GList *props);
gboolean kafka_options_set_logging(KafkaOptions *self, const gchar *logging);
void kafka_options_set_bootstrap_servers(KafkaOptions *self, const gchar *bootstrap_servers);
void kafka_options_set_poll_timeout(KafkaOptions *self, gint poll_timeout);

#endif
