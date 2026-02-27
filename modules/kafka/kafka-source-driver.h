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

#ifndef KAFKA_SOURCE_DRIVER_H_INCLUDED
#define KAFKA_SOURCE_DRIVER_H_INCLUDED

#include "syslog-ng.h"
#include "driver.h"
#include "logsource.h"

typedef struct _KafkaSourceOptions KafkaSourceOptions;
typedef struct _KafkaSourceDriver KafkaSourceDriver;

void kafka_sd_merge_config(LogDriver *d, GList *props);
gboolean kafka_sd_set_logging(LogDriver *d, const gchar *logging);
gboolean kafka_sd_set_topics(LogDriver *d, GList *topics);
gboolean kafka_sd_set_strategy_hint(LogDriver *d, const gchar *strategy_hint);
gboolean kafka_sd_set_persis_store(LogDriver *d, const gchar *strategy_hint);
void kafka_sd_set_bootstrap_servers(LogDriver *d, const gchar *bootstrap_servers);
void kafka_sd_set_log_fetch_delay(LogDriver *s, guint new_value);
void kafka_sd_set_log_fetch_retry_delay(LogDriver *s, guint new_value);
void kafka_sd_set_log_fetch_limit(LogDriver *s, guint new_value);
void kafka_sd_set_log_fetch_queue_full_delay(LogDriver *s, guint new_value);
void kafka_sd_set_poll_timeout(LogDriver *d, gint poll_timeout);
void kafka_sd_set_state_update_timeout(LogDriver *d, gint state_update_timeout);
void kafka_sd_set_time_reopen(LogDriver *d, gint time_reopen);
void kafka_sd_set_ignore_saved_bookmarks(LogDriver *s, gboolean new_value);
void kafka_sd_set_disable_bookmarks(LogDriver *s, gboolean new_value);
void kafka_sd_set_separate_worker_queues(LogDriver *s, gboolean new_value);
void kafka_sd_set_store_kafka_metadata(LogDriver *s, gboolean new_value);

LogDriver *kafka_sd_new(GlobalConfig *cfg);

#endif
