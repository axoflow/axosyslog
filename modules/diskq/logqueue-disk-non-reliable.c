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

#include "logqueue-disk-non-reliable.h"
#include "logpipe.h"
#include "messages.h"
#include "syslog-ng.h"
#include "scratch-buffers.h"

static void
_init_memory_queue(LogQueueDiskMemoryQueue *q)
{
  INIT_IV_LIST_HEAD(&q->items);
}

static void
_push_to_memory_queue_tail(LogQueueDiskMemoryQueue *q, LogMessage *msg, const LogPathOptions *path_options)
{
  LogMessageQueueNode *node;

  node = log_msg_alloc_queue_node(msg, path_options);
  iv_list_add_tail(&node->list, &q->items);
  q->len++;
}

static void
_push_node_to_memory_queue_tail(LogQueueDiskMemoryQueue *q, LogMessageQueueNode *node)
{
  iv_list_add_tail(&node->list, &q->items);
  q->len++;
}

static void
_push_node_to_memory_queue_head(LogQueueDiskMemoryQueue *q, LogMessageQueueNode *node)
{
  iv_list_add(&node->list, &q->items);
  q->len++;
}

static gboolean
_peek_from_memory_queue_head(LogQueueDiskMemoryQueue *memory_queue, LogMessage **pmsg)
{
  if (memory_queue->len > 0)
    {
      LogMessageQueueNode *node = iv_list_entry(memory_queue->items.next, LogMessageQueueNode, list);
      *pmsg = log_msg_ref(node->msg);
      return TRUE;
    }
  return FALSE;
}

static gboolean
_pop_node_from_memory_queue_head(LogQueueDiskMemoryQueue *memory_queue, LogMessageQueueNode **node)
{
  if (memory_queue->len > 0)
    {
      *node = iv_list_entry(memory_queue->items.next, LogMessageQueueNode, list);
      memory_queue->len--;
      iv_list_del_init(&(*node)->list);

      return TRUE;
    }
  return FALSE;
}

static void
_extract_queue_node(LogMessageQueueNode *node, LogMessage **pmsg, LogPathOptions *path_options)
{
  *pmsg = log_msg_ref(node->msg);
  path_options->ack_needed = node->ack_needed;
}

static gboolean
_pop_from_memory_queue_head(LogQueueDiskMemoryQueue *memory_queue, LogMessage **pmsg, LogPathOptions *path_options)
{
  LogMessageQueueNode *node;

  if (!_pop_node_from_memory_queue_head(memory_queue, &node))
    return FALSE;
  _extract_queue_node(node, pmsg, path_options);
  log_msg_free_queue_node(node);
  return TRUE;
}

static void
_update_memory_usage_during_load(LogQueueDiskNonReliable *s, LogQueueDiskMemoryQueue *memory_queue, guint offset)
{
  if (memory_queue->len == offset)
    return;

  struct iv_list_head *ilh;
  gsize i = 0;
  iv_list_for_each(ilh, &memory_queue->items)
    {
      if (i >= offset)
        {
          LogMessage *msg = iv_list_entry(ilh, LogMessageQueueNode, list)->msg;

          log_queue_memory_usage_add(&s->super.super, log_msg_get_size(msg));
        }
    }
}

static LogQueueDiskMemoryQueue *
_qtype_to_memory_queue(LogQueueDiskNonReliable *self, QDiskMemoryQueueType type)
{
  switch (type)
    {
    case QDISK_MQ_FRONT_CACHE:
      return &self->front_cache;
    case QDISK_MQ_BACKLOG:
      return &self->backlog;
    case QDISK_MQ_FLOW_CONTROL_WINDOW:
      return &self->flow_control_window;
    default:
      g_assert_not_reached();
    }
}

static gboolean
_load_queue_callback(QDiskMemoryQueueType qtype, LogMessage *msg, gpointer user_data)
{
  LogQueueDiskNonReliable *self = (LogQueueDiskNonReliable *) user_data;
  LogQueueDiskMemoryQueue *memq = _qtype_to_memory_queue(self, qtype);

  _push_to_memory_queue_tail(memq, msg, NULL);
  log_msg_unref(msg);
  return TRUE;
}

static gboolean
_start(LogQueueDisk *s)
{
  LogQueueDiskNonReliable *self = (LogQueueDiskNonReliable *) s;

  guint front_cache_length_before_start = self->front_cache.len;
  guint backlog_length_before_start = self->backlog.len;
  guint flow_control_window_length_before_start = self->flow_control_window.len;

  gboolean retval = qdisk_start(s->qdisk, _load_queue_callback, self);

  _update_memory_usage_during_load(self, &self->front_cache, front_cache_length_before_start);
  _update_memory_usage_during_load(self, &self->backlog, backlog_length_before_start);
  _update_memory_usage_during_load(self, &self->flow_control_window, flow_control_window_length_before_start);

  return retval;
}

static inline gboolean
_has_space_in_front_cache(LogQueueDiskNonReliable *self)
{
  return self->front_cache.len < self->front_cache_size;
}

static gint64
_get_length(LogQueue *s)
{
  LogQueueDiskNonReliable *self = (LogQueueDiskNonReliable *)s;

  if (!qdisk_started(self->super.qdisk))
    return 0;

  return self->front_cache.len
         + qdisk_get_length(self->super.qdisk)
         + self->flow_control_window.len;
}

static inline gboolean
_can_push_to_front_cache(LogQueueDiskNonReliable *self)
{
  return _has_space_in_front_cache(self) && qdisk_get_length(self->super.qdisk) == 0;
}

static inline gboolean
_flow_control_window_has_movable_message(LogQueueDiskNonReliable *self)
{
  return self->flow_control_window.len > 0
         && (_can_push_to_front_cache(self) || qdisk_is_space_avail(self->super.qdisk, 4096));
}

static gboolean
_serialize_and_write_message_to_disk(LogQueueDiskNonReliable *self, LogMessage *msg)
{
  ScratchBuffersMarker marker;
  GString *write_serialized = scratch_buffers_alloc_and_mark(&marker);
  if (!log_queue_disk_serialize_msg(&self->super, msg, write_serialized))
    {
      scratch_buffers_reclaim_marked(marker);
      return FALSE;
    }

  gboolean success = qdisk_push_tail(self->super.qdisk, write_serialized);

  scratch_buffers_reclaim_marked(marker);
  return success;
}

static void
_move_messages_from_overflow(LogQueueDiskNonReliable *self)
{
  LogMessage *msg;
  LogPathOptions path_options = LOG_PATH_OPTIONS_INIT;
  /* move away as much entries from the overflow area as possible */
  while (_flow_control_window_has_movable_message(self))
    {
      LogMessageQueueNode *node;

      if (!_pop_node_from_memory_queue_head(&self->flow_control_window, &node))
        break;

      /* NOTE: this adds a ref to msg */
      _extract_queue_node(node, &msg, &path_options);
      if (_can_push_to_front_cache(self))
        {
          /* we can skip qdisk, go straight to front_cache */
          _push_node_to_memory_queue_tail(&self->front_cache, node);
          log_msg_ack(msg, &path_options, AT_PROCESSED);
          log_msg_unref(msg);
        }
      else
        {
          if (_serialize_and_write_message_to_disk(self, msg))
            {
              log_queue_disk_update_disk_related_counters(&self->super);
              log_queue_memory_usage_sub(&self->super.super, log_msg_get_size(msg));
              log_msg_ack(msg, &path_options, AT_PROCESSED);
              log_msg_free_queue_node(node);
              log_msg_unref(msg);
            }
          else
            {
              /* oops, although there seemed to be some free space available,
               * we failed saving this message, (it might have needed more
               * than 4096 bytes than we ensured), push back and break
               */
              _push_node_to_memory_queue_head(&self->flow_control_window, node);
              log_msg_unref(msg);
              break;
            }
        }
    }
}

static gboolean
_move_messages_from_disk_to_front_cache(LogQueueDiskNonReliable *self)
{
  do
    {
      if (qdisk_get_length(self->super.qdisk) <= 0)
        break;

      LogPathOptions path_options;
      LogMessage *msg = log_queue_disk_read_message(&self->super, &path_options);

      if (!msg)
        return FALSE;

      _push_to_memory_queue_tail(&self->front_cache, msg, &path_options);
      log_queue_memory_usage_add(&self->super.super, log_msg_get_size(msg));
      log_msg_unref(msg);

      log_queue_disk_update_disk_related_counters(&self->super);
    }
  while (_has_space_in_front_cache(self));

  return TRUE;
}

static inline gboolean
_maybe_move_messages_among_queue_segments(LogQueueDiskNonReliable *self)
{
  gboolean ret = TRUE;

  if (qdisk_is_read_only(self->super.qdisk))
    return TRUE;

  if (self->front_cache.len == 0 && self->front_cache_size > 0)
    ret = _move_messages_from_disk_to_front_cache(self);

  if (self->flow_control_window.len > 0)
    _move_messages_from_overflow(self);

  return ret;
}

/* runs only in the output thread */
static void
_ack_backlog(LogQueue *s, gint num_msg_to_ack)
{
  LogQueueDiskNonReliable *self = (LogQueueDiskNonReliable *)s;
  LogMessage *msg;
  LogPathOptions path_options = LOG_PATH_OPTIONS_INIT;
  guint i;

  for (i = 0; i < num_msg_to_ack; i++)
    {
      if (!_pop_from_memory_queue_head(&self->backlog, &msg, &path_options))
        return;

      log_queue_memory_usage_sub(&self->super.super, log_msg_get_size(msg));
      log_msg_ack(msg, &path_options, AT_PROCESSED);
      log_msg_unref(msg);
    }
}

static void
_rewind_backlog(LogQueue *s, guint rewind_count)
{
  guint i;
  LogQueueDiskNonReliable *self = (LogQueueDiskNonReliable *)s;

  g_mutex_lock(&s->lock);

  /* TODO: instead of iterating the messages in the backlog, just move it in
   * one swoop using iv_list_splice() */

  rewind_count = MIN(rewind_count, self->backlog.len);

  for (i = 0; i < rewind_count; i++)
    {
      LogMessageQueueNode *node;
      if (!_pop_node_from_memory_queue_head(&self->backlog, &node))
        return;

      _push_node_to_memory_queue_head(&self->front_cache, node);
      log_queue_queued_messages_inc(s);
    }

  g_mutex_unlock(&s->lock);
}

static void
_rewind_backlog_all(LogQueue *s)
{
  _rewind_backlog(s, G_MAXUINT);
}

static inline LogMessage *
_pop_head_front_cache(LogQueueDiskNonReliable *self, LogPathOptions *path_options)
{
  LogMessage *msg;

  if (!_pop_from_memory_queue_head(&self->front_cache, &msg, path_options))
    return NULL;
  log_queue_memory_usage_sub(&self->super.super, log_msg_get_size(msg));

  return msg;
}

static inline LogMessage *
_pop_head_flow_control_window(LogQueueDiskNonReliable *self, LogPathOptions *path_options)
{
  LogMessage *msg;

  if (!_pop_from_memory_queue_head(&self->flow_control_window, &msg, path_options))
    return NULL;
  log_queue_memory_usage_sub(&self->super.super, log_msg_get_size(msg));

  return msg;
}

/* runs only in the output thread */
static inline void
_push_tail_backlog(LogQueueDiskNonReliable *self, LogMessage *msg, LogPathOptions *path_options)
{
  _push_to_memory_queue_tail(&self->backlog, msg, path_options);
  log_queue_memory_usage_add(&self->super.super, log_msg_get_size(msg));
}

static LogMessage *
_peek_head(LogQueue *s)
{
  LogQueueDiskNonReliable *self = (LogQueueDiskNonReliable *)s;
  LogMessage *msg = NULL;

  g_mutex_lock(&s->lock);

  if (_peek_from_memory_queue_head(&self->front_cache, &msg))
    goto success;

  msg = log_queue_disk_peek_message(&self->super);
  if (msg)
    goto success;

  if (qdisk_is_read_only(self->super.qdisk) && _peek_from_memory_queue_head(&self->flow_control_window, &msg))
    goto success;

success:
  g_mutex_unlock(&s->lock);
  return msg;
}

static LogMessage *
_pop_head(LogQueue *s, LogPathOptions *path_options)
{
  LogQueueDiskNonReliable *self = (LogQueueDiskNonReliable *)s;
  LogMessage *msg = NULL;
  gboolean stats_update = TRUE;

  g_mutex_lock(&s->lock);

  if (self->front_cache.len > 0)
    {
      msg = _pop_head_front_cache(self, path_options);
      if (msg)
        goto success;
    }

  msg = log_queue_disk_read_message(&self->super, path_options);
  if (msg)
    goto success;

  if (self->flow_control_window.len > 0 && qdisk_is_read_only(self->super.qdisk))
    msg = _pop_head_flow_control_window(self, path_options);

  if (!msg)
    {
      g_mutex_unlock(&s->lock);
      return NULL;
    }

success:
  if (!_maybe_move_messages_among_queue_segments(self))
    {
      stats_update = FALSE;
    }

  log_queue_disk_update_disk_related_counters(&self->super);
  g_mutex_unlock(&s->lock);

  _push_tail_backlog(self, msg, path_options);

  if (stats_update)
    log_queue_queued_messages_dec(s);

  return msg;
}

/* _is_msg_serialization_needed_hint() must be called without holding the queue's lock.
 * This can only be used _as a hint_ for performance considerations, because as soon as the lock
 * is released, there will be no guarantee that the result of this function remain correct. */
static inline gboolean
_is_msg_serialization_needed_hint(LogQueueDiskNonReliable *self)
{
  g_mutex_lock(&self->super.super.lock);

  gboolean msg_serialization_needed = FALSE;

  if (_can_push_to_front_cache(self))
    goto exit;

  if (self->flow_control_window.len != 0)
    goto exit;

  if (!qdisk_started(self->super.qdisk) || !qdisk_is_space_avail(self->super.qdisk, 64))
    goto exit;

  msg_serialization_needed = TRUE;

exit:
  g_mutex_unlock(&self->super.super.lock);
  return msg_serialization_needed;
}

static gboolean
_ensure_serialized_and_write_to_disk(LogQueueDiskNonReliable *self, LogMessage *msg, GString *serialized_msg)
{
  if (serialized_msg)
    return qdisk_push_tail(self->super.qdisk, serialized_msg);

  return _serialize_and_write_message_to_disk(self, msg);
}

static inline void
_push_tail_front_cache(LogQueueDiskNonReliable *self, LogMessage *msg, const LogPathOptions *path_options)
{
  /* simple push never generates flow-control enabled entries to front_cache, they only get there
   * when rewinding the backlog */
  _push_to_memory_queue_tail(&self->front_cache, msg, NULL);
  log_queue_memory_usage_add(&self->super.super, log_msg_get_size(msg));

  log_msg_ack(msg, path_options, AT_PROCESSED);
  log_msg_unref(msg);
}

static inline void
_push_tail_flow_control_window(LogQueueDiskNonReliable *self, LogMessage *msg, const LogPathOptions *path_options)
{
  _push_to_memory_queue_tail(&self->flow_control_window, msg, path_options);
  log_queue_memory_usage_add(&self->super.super, log_msg_get_size(msg));
  /* no ack */
  log_msg_unref(msg);
}

static inline gboolean
_push_tail_disk(LogQueueDiskNonReliable *self, LogMessage *msg, const LogPathOptions *path_options,
                GString *serialized_msg)
{
  gboolean result = _ensure_serialized_and_write_to_disk(self, msg, serialized_msg);
  if (result)
    {
      log_msg_ack(msg, path_options, AT_PROCESSED);
      log_msg_unref(msg);
    }

  log_queue_disk_update_disk_related_counters(&self->super);

  return result;
}

static void
_push_tail(LogQueue *s, LogMessage *msg, const LogPathOptions *path_options)
{
  LogQueueDiskNonReliable *self = (LogQueueDiskNonReliable *)s;

  ScratchBuffersMarker marker;
  GString *serialized_msg = NULL;

  if (_is_msg_serialization_needed_hint(self))
    {
      serialized_msg = scratch_buffers_alloc_and_mark(&marker);
      if (!log_queue_disk_serialize_msg(&self->super, msg, serialized_msg))
        {
          msg_error("Failed to serialize message for non-reliable disk-buffer, dropping message",
                    evt_tag_str("filename", qdisk_get_filename(self->super.qdisk)),
                    evt_tag_str("persist_name", s->persist_name));
          log_queue_disk_drop_message(&self->super, msg, path_options);
          scratch_buffers_reclaim_marked(marker);
          return;
        }
    }

  g_mutex_lock(&s->lock);

  /* we push messages into queue segments in the following order: flow_control_window, disk, front_cache */
  if (_can_push_to_front_cache(self))
    {
      _push_tail_front_cache(self, msg, path_options);
      goto queued;
    }

  if (self->flow_control_window.len != 0 || !_push_tail_disk(self, msg, path_options, serialized_msg))
    {
      if (path_options->flow_control_requested)
        {
          _push_tail_flow_control_window(self, msg, path_options);
          goto queued;
        }

      msg_debug("Destination queue full, dropping message",
                evt_tag_str("filename", qdisk_get_filename(self->super.qdisk)),
                evt_tag_long("queue_len", log_queue_get_length(s)),
                evt_tag_long("capacity_bytes", qdisk_get_maximum_size(self->super.qdisk)),
                evt_tag_str("persist_name", s->persist_name));
      log_queue_disk_drop_message(&self->super, msg, path_options);
      goto exit;
    }

queued:
  log_queue_queued_messages_inc(s);

  /* this releases the queue's lock for a short time, which may violate the
   * consistency of the disk-buffer, so it must be the last call under lock in this function
   */
  log_queue_push_notify(s);

exit:
  g_mutex_unlock(&s->lock);
  if (serialized_msg)
    scratch_buffers_reclaim_marked(marker);
}

static void
_empty_queue(LogQueueDiskNonReliable *self, LogQueueDiskMemoryQueue *q)
{
  while (q && q->len > 0)
    {
      LogMessage *msg;
      LogPathOptions path_options = LOG_PATH_OPTIONS_INIT;

      _pop_from_memory_queue_head(q, &msg, &path_options);
      log_queue_memory_usage_sub(&self->super.super, log_msg_get_size(msg));
      log_msg_ack(msg, &path_options, AT_PROCESSED);
      log_msg_unref(msg);
    }
}

static void
_free(LogQueue *s)
{
  LogQueueDiskNonReliable *self = (LogQueueDiskNonReliable *)s;

  g_assert(self->front_cache.len == 0);
  g_assert(self->backlog.len == 0);
  g_assert(self->flow_control_window.len == 0);
  log_queue_disk_free_method(&self->super);
}

static gboolean
_save_queue_callback(QDiskMemoryQueueType qtype, LogMessage **pmsg, gpointer *iter, gpointer user_data)
{
  LogQueueDiskNonReliable *self = (LogQueueDiskNonReliable *) user_data;
  LogQueueDiskMemoryQueue *memq = _qtype_to_memory_queue(self, qtype);
  struct iv_list_head **ilh = (struct iv_list_head **) iter;

  if (*ilh == NULL)
    *ilh = memq->items.next;
  if (*ilh != &memq->items)
    {
      LogMessageQueueNode *node = iv_list_entry(*ilh, LogMessageQueueNode, list);
      *pmsg = log_msg_ref(node->msg);
      *ilh = (*ilh)->next;
      return TRUE;
    }
  return FALSE;
}

static gboolean
_stop(LogQueueDisk *s, gboolean *persistent)
{
  LogQueueDiskNonReliable *self = (LogQueueDiskNonReliable *) s;

  gboolean result = FALSE;

  if (qdisk_stop(s->qdisk, _save_queue_callback, self))
    {
      *persistent = TRUE;
      result = TRUE;
    }

  _empty_queue(self, &self->flow_control_window);
  _empty_queue(self, &self->front_cache);
  _empty_queue(self, &self->backlog);

  return result;
}

static gboolean
_stop_corrupted(LogQueueDisk *s)
{
  return qdisk_stop(s->qdisk, NULL, NULL);
}

static inline void
_set_logqueue_virtual_functions(LogQueue *s)
{
  s->get_length = _get_length;
  s->ack_backlog = _ack_backlog;
  s->rewind_backlog = _rewind_backlog;
  s->rewind_backlog_all = _rewind_backlog_all;
  s->pop_head = _pop_head;
  s->peek_head = _peek_head;
  s->push_tail = _push_tail;
  s->free_fn = _free;
}

static inline void
_set_logqueue_disk_virtual_functions(LogQueueDisk *s)
{
  s->start = _start;
  s->stop = _stop;
  s->stop_corrupted = _stop_corrupted;
}

static inline void
_set_virtual_functions(LogQueueDiskNonReliable *self)
{
  _set_logqueue_virtual_functions(&self->super.super);
  _set_logqueue_disk_virtual_functions(&self->super);
}

LogQueue *
log_queue_disk_non_reliable_new(DiskQueueOptions *options, const gchar *filename, const gchar *persist_name,
                                gint stats_level, StatsClusterKeyBuilder *driver_sck_builder,
                                StatsClusterKeyBuilder *queue_sck_builder)
{
  g_assert(options->reliable == FALSE);
  LogQueueDiskNonReliable *self = g_new0(LogQueueDiskNonReliable, 1);
  log_queue_disk_init_instance(&self->super, options, "SLQF", filename, persist_name, stats_level,
                               driver_sck_builder, queue_sck_builder);
  _init_memory_queue(&self->front_cache);
  _init_memory_queue(&self->backlog);
  _init_memory_queue(&self->flow_control_window);
  self->front_cache_size = options->front_cache_size;
  _set_virtual_functions(self);
  return &self->super.super;
}
