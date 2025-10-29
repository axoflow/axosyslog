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

#ifndef KAFKA_SOURCE_PERSIST_H
#define KAFKA_SOURCE_PERSIST_H

#include "syslog-ng.h"
#include "persist-state.h"
#include "kafka-source-driver.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#include <librdkafka/rdkafka.h>
#pragma GCC diagnostic pop

typedef struct _KafkaSourcePersist KafkaSourcePersist;

KafkaSourcePersist *kafka_source_persist_new(KafkaSourceDriver *owner);
void kafka_source_persist_ref(KafkaSourcePersist *self);
void kafka_source_persist_unref(KafkaSourcePersist *self);
gboolean kafka_source_persist_init(KafkaSourcePersist *self,
                                   PersistState *state,
                                   const gchar *topic,
                                   int32_t partition,
                                   int64_t override_position,
                                   gboolean use_offset_tracker);
void kafka_source_persist_invalidate(KafkaSourcePersist *self);
gboolean kafka_source_persist_is_ready(KafkaSourcePersist *self);
gboolean kafka_source_persist_matching(KafkaSourcePersist *self,
                                       const gchar *topic,
                                       int32_t partition);
/* Lock must be held before calling */
gboolean kafka_source_persist_remote_is_valid(KafkaSourcePersist *self);
int32_t kafka_source_persist_get_partition(KafkaSourcePersist *self);
const gchar *kafka_source_persist_get_topic(KafkaSourcePersist *self);

void kafka_source_persist_fill_bookmark(KafkaSourcePersist *self,
                                        Bookmark *bookmark,
                                        int64_t offset);
void kafka_source_persist_load_position(KafkaSourcePersist *self,
                                        int64_t *offset);

#endif
