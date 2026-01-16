/*
 * Copyright (c) 2002-2016 Balabit
 * Copyright (c) 2016 Viktor Juhasz <viktor.juhasz@balabit.com>
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

#ifndef QDISK_H_
#define QDISK_H_

#include "syslog-ng.h"
#include "diskq-options.h"

#define QDISK_RESERVED_SPACE 4096

typedef enum
{
  QDISK_MQ_FRONT_CACHE,
  QDISK_MQ_BACKLOG,
  QDISK_MQ_FLOW_CONTROL_WINDOW,
} QDiskMemoryQueueType;

typedef struct
{
  gint64 ofs;
  guint32 len;
  guint32 count;
} QDiskQueuePosition;

typedef union _QDiskFileHeader
{
  struct
  {
    gchar magic[4];
    guint8 version;
    guint8 big_endian;
    guint8 _pad1;

    gint64 read_head;
    gint64 write_head;
    gint64 length;

    QDiskQueuePosition front_cache_pos;
    QDiskQueuePosition backlog_pos;
    QDiskQueuePosition flow_control_window_pos;
    gint64 backlog_head;
    gint64 backlog_len;

    guint8 use_v1_wrap_condition;
    gint64 capacity_bytes;
  };
  gchar _pad2[QDISK_RESERVED_SPACE];
} QDiskFileHeader;

typedef gboolean (*QDiskSerializeFunc)(SerializeArchive *sa, gpointer user_data);
typedef gboolean (*QDiskDeSerializeFunc)(SerializeArchive *sa, gpointer user_data);
typedef gboolean (*QDiskMemQLoadFunc)(QDiskMemoryQueueType type, LogMessage *msg, gpointer user_data);
typedef gboolean (*QDiskMemQSaveFunc)(QDiskMemoryQueueType type, LogMessage **pmsg, gpointer *iter, gpointer user_data);

typedef struct _QDisk QDisk;

QDisk *qdisk_new(DiskQueueOptions *options, const gchar *file_id, const gchar *filename);

gboolean qdisk_is_space_avail(QDisk *self, gint at_least);
gint64 qdisk_get_max_useful_space(QDisk *self);
gint64 qdisk_get_empty_space(QDisk *self);
gint64 qdisk_get_used_useful_space(QDisk *self);
gboolean qdisk_push_tail(QDisk *self, GString *record);
gboolean qdisk_pop_head(QDisk *self, GString *record);
gboolean qdisk_peek_head(QDisk *self, GString *record);
gboolean qdisk_remove_head(QDisk *self);
gboolean qdisk_ack_backlog(QDisk *self);
gboolean qdisk_rewind_backlog(QDisk *self, guint rewind_count);
void qdisk_empty_backlog(QDisk *self);
gint64 qdisk_get_next_tail_position(QDisk *self);
gint64 qdisk_get_next_head_position(QDisk *self);
gboolean qdisk_load_hdr(QDisk *self);
gboolean qdisk_unload_hdr(QDisk *self);
gboolean qdisk_start(QDisk *self, QDiskMemQLoadFunc func, gpointer user_data);
gboolean qdisk_stop(QDisk *self, QDiskMemQSaveFunc func, gpointer user_data);
void qdisk_reset_file_if_empty(QDisk *self);
gboolean qdisk_started(QDisk *self);
void qdisk_free(QDisk *self);

DiskQueueOptions *qdisk_get_options(QDisk *self);
gint64 qdisk_get_length(QDisk *self);
gint64 qdisk_get_hdr_total_length(QDisk *self);
gint64 qdisk_get_maximum_size(QDisk *self);
gint64 qdisk_get_writer_head(QDisk *self);
gint64 qdisk_get_reader_head(QDisk *self);
gint64 qdisk_get_backlog_head(QDisk *self);
gint64 qdisk_get_backlog_count(QDisk *self);
gint qdisk_get_flow_control_window_bytes(QDisk *self);
gboolean qdisk_is_read_only(QDisk *self);
const gchar *qdisk_get_filename(QDisk *self);
gint64 qdisk_get_file_size(QDisk *self);

gchar *qdisk_get_next_filename(const gchar *dir, gboolean reliable);
gboolean qdisk_is_file_a_disk_buffer_file(const gchar *filename);
gboolean qdisk_is_disk_buffer_file_reliable(const gchar *filename, gboolean *reliable);

gboolean qdisk_serialize(GString *serialized, QDiskSerializeFunc serialize_func, gpointer user_data, GError **error);
gboolean qdisk_deserialize(GString *serialized, QDiskDeSerializeFunc deserialize_func, gpointer user_data,
                           GError **error);

#endif /* QDISK_H_ */
