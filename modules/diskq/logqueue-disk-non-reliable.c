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
  q->limit = G_MAXINT;
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

static LogQueueDiskMemoryQueue *
_qtype_to_memory_queue(LogQueueDiskNonReliable *self, QDiskMemoryQueueType type)
{
  switch (type)
    {
    case QDISK_MQ_FRONT_CACHE:
      return &self->front_cache_output;
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
  log_queue_memory_usage_add(&self->super.super, log_msg_get_size(msg));
  log_msg_unref(msg);
  return TRUE;
}

static gboolean
_start(LogQueueDisk *s)
{
  LogQueueDiskNonReliable *self = (LogQueueDiskNonReliable *) s;

  gboolean retval = qdisk_start(s->qdisk, _load_queue_callback, self);
  return retval;
}

static gint64
_get_length(LogQueue *s)
{
  LogQueueDiskNonReliable *self = (LogQueueDiskNonReliable *)s;

  if (!qdisk_started(self->super.qdisk))
    return 0;

  return self->front_cache.len
         + self->front_cache_output.len
         + qdisk_get_length(self->super.qdisk)
         + self->flow_control_window.len;
}

static inline gboolean
_can_push_to_front_cache_queue(LogQueueDiskNonReliable *self, LogQueueDiskMemoryQueue *queue)
{
  return queue->len < queue->limit && qdisk_get_length(self->super.qdisk) == 0;
}

static inline gboolean
_can_push_to_front_cache(LogQueueDiskNonReliable *self)
{
  return _can_push_to_front_cache_queue(self, &self->front_cache);
}

static inline gboolean
_can_push_to_front_cache_output(LogQueueDiskNonReliable *self)
{
  return _can_push_to_front_cache_queue(self, &self->front_cache_output);
}

static inline gboolean
_flow_control_window_has_movable_message(LogQueueDiskNonReliable *self)
{
  return self->flow_control_window.len > 0
         && (_can_push_to_front_cache_output(self) || _can_push_to_front_cache(self)
             || qdisk_is_space_avail(self->super.qdisk, 4096));
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

      if (_can_push_to_front_cache_output(self))
        {
          /* we can skip qdisk and front_cache, go straight to front_cache_output */
          _push_node_to_memory_queue_tail(&self->front_cache_output, node);
          log_msg_ack(msg, &path_options, AT_PROCESSED);
          log_msg_unref(msg);
          continue;
        }

      if (_can_push_to_front_cache(self))
        {
          /* we can skip qdisk, go straight to front_cache */
          _push_node_to_memory_queue_tail(&self->front_cache, node);
          log_msg_ack(msg, &path_options, AT_PROCESSED);
          log_msg_unref(msg);
          continue;
        }

      if (_serialize_and_write_message_to_disk(self, msg))
        {
          log_queue_disk_update_disk_related_counters(&self->super);
          log_queue_memory_usage_sub(&self->super.super, log_msg_get_size(msg));
          log_msg_ack(msg, &path_options, AT_PROCESSED);
          log_msg_free_queue_node(node);
          log_msg_unref(msg);
          continue;
        }

      /*
       * oops, although there seemed to be some free space available,
       * we failed saving this message, (it might have needed more
       * than 4096 bytes than we ensured), push back and break
       */
      _push_node_to_memory_queue_head(&self->flow_control_window, node);
      log_msg_unref(msg);
      break;
    }
}

static gboolean
_move_messages_from_disk_to_front_cache_queue(LogQueueDiskNonReliable *self, LogQueueDiskMemoryQueue *queue)
{
  do
    {
      if (qdisk_get_length(self->super.qdisk) <= 0)
        break;

      LogPathOptions path_options;
      LogMessage *msg = log_queue_disk_read_message(&self->super, &path_options);

      if (!msg)
        return FALSE;

      _push_to_memory_queue_tail(queue, msg, &path_options);
      log_queue_memory_usage_add(&self->super.super, log_msg_get_size(msg));
      log_msg_unref(msg);

      log_queue_disk_update_disk_related_counters(&self->super);
    }
  while (queue->len < queue->limit);

  return TRUE;
}

static inline gboolean
_move_messages_from_disk_to_front_cache(LogQueueDiskNonReliable *self)
{
  return _move_messages_from_disk_to_front_cache_queue(self, &self->front_cache);
}

static inline gboolean
_move_messages_from_disk_to_front_cache_output(LogQueueDiskNonReliable *self)
{
  return _move_messages_from_disk_to_front_cache_queue(self, &self->front_cache_output);
}

/*
 * Can only run from the output thread.
 */
static inline void
_move_items_from_front_cache_queue_to_output_queue(LogQueueDiskNonReliable *self)
{
  iv_list_splice_tail_init(&self->front_cache.items, &self->front_cache_output.items);
  self->front_cache_output.len = self->front_cache.len;
  self->front_cache.len = 0;
}

static inline gboolean
_is_front_cache_enabled(LogQueueDiskNonReliable *self)
{
  return self->front_cache.limit > 0;
}

/*
 * Must be called with lock held.
 */
static inline gboolean
_maybe_move_messages_among_queue_segments(LogQueueDiskNonReliable *self)
{
  gboolean ret = TRUE;

  if (qdisk_is_read_only(self->super.qdisk))
    return TRUE;

  if (_is_front_cache_enabled(self))
    {
      if (self->front_cache_output.len == 0 && self->front_cache.len > 0)
        _move_items_from_front_cache_queue_to_output_queue(self);

      ret = _move_messages_from_disk_to_front_cache_output(self);
      if (ret)
        ret = _move_messages_from_disk_to_front_cache(self);
    }

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

/* runs only in the output thread */
static void
_rewind_backlog(LogQueue *s, guint rewind_count)
{
  guint i;
  LogQueueDiskNonReliable *self = (LogQueueDiskNonReliable *)s;

  /* TODO: instead of iterating the messages in the backlog, just move it in
   * one swoop using iv_list_splice() */

  rewind_count = MIN(rewind_count, self->backlog.len);

  for (i = 0; i < rewind_count; i++)
    {
      LogMessageQueueNode *node;
      if (!_pop_node_from_memory_queue_head(&self->backlog, &node))
        return;

      _push_node_to_memory_queue_head(&self->front_cache_output, node);
      log_queue_queued_messages_inc(s);
    }
}

/* runs only in the output thread */
static void
_rewind_backlog_all(LogQueue *s)
{
  _rewind_backlog(s, G_MAXUINT);
}

static inline LogMessage *
_pop_head_front_cache_queues(LogQueueDiskNonReliable *self, LogPathOptions *path_options,
                             LogQueueDiskMemoryQueue *queue)
{
  LogMessage *msg;

  if (!_pop_from_memory_queue_head(queue, &msg, path_options))
    return NULL;
  log_queue_memory_usage_sub(&self->super.super, log_msg_get_size(msg));

  return msg;
}

static inline LogMessage *
_pop_head_front_cache(LogQueueDiskNonReliable *self, LogPathOptions *path_options)
{
  return _pop_head_front_cache_queues(self, path_options, &self->front_cache);
}

static inline LogMessage *
_pop_head_front_cache_output(LogQueueDiskNonReliable *self, LogPathOptions *path_options)
{
  return _pop_head_front_cache_queues(self, path_options, &self->front_cache_output);
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

  if (_peek_from_memory_queue_head(&self->front_cache_output, &msg))
    return msg;

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

  msg = _pop_head_front_cache_output(self, path_options);
  if (msg && self->front_cache_output.len != 0)
    {
      /*
       * FAST PATH
       *
       * Successfully popped a message from the output queue
       * and there are still more in it for the next pop.
       *
       * Let's just return the message without grabbing the lock
       * and moving messages around in the queue segments.
       */
      goto fast_success;
    }

  g_mutex_lock(&s->lock);

  if (msg)
    goto slow_success;

  msg = _pop_head_front_cache(self, path_options);
  if (msg)
    goto slow_success;

  msg = log_queue_disk_read_message(&self->super, path_options);
  if (msg)
    goto slow_success;

  if (self->flow_control_window.len > 0 && qdisk_is_read_only(self->super.qdisk))
    msg = _pop_head_flow_control_window(self, path_options);

  if (!msg)
    {
      g_mutex_unlock(&s->lock);
      return NULL;
    }

slow_success:
  if (!_maybe_move_messages_among_queue_segments(self))
    {
      stats_update = FALSE;
    }

  log_queue_disk_update_disk_related_counters(&self->super);
  g_mutex_unlock(&s->lock);

fast_success:
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
_push_tail_single_message(LogQueue *s, LogMessage *msg, const LogPathOptions *path_options)
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
_push_tail(LogQueue *s, LogMessage *msg, const LogPathOptions *path_options)
{
  LogQueueDiskNonReliable *self = (LogQueueDiskNonReliable *) s;
  gint thread_index;
  LogMessageQueueNode *node;

  thread_index = main_loop_worker_get_thread_index();

  /* if this thread has an ID than the number of input queues we have (due
   * to a config change), handle the load via the slow path */

  if (thread_index >= self->num_input_queues)
    thread_index = -1;

  /* NOTE: we don't use high-water marks for now, as log_fetch_limit
   * limits the number of items placed on the per-thread input queue
   * anyway, and any sane number decreased the performance measurably.
   *
   * This means that per-thread input queues contain _all_ items that
   * a single poll iteration produces. And once the reader is finished
   * (either because the input is depleted or because of
   * log_fetch_limit / window_size) the whole bunch is shared between the
   * front_cache and legacy behaviour.
   */

  if (thread_index >= 0)
    {
      /* fastpath, use per-thread input FIFOs */
      if (!self->input_queues[thread_index].finish_cb_registered)
        {
          /* this is the first item in the input FIFO, register a finish
           * callback to make sure it gets moved to the wait_queue if the
           * input thread finishes
           * One reference should be held, while the callback is registered
           * avoiding use-after-free situation
           */

          main_loop_worker_register_batch_callback(&self->input_queues[thread_index].cb);
          self->input_queues[thread_index].finish_cb_registered = TRUE;
          log_queue_ref(&self->super.super);
        }

      log_msg_write_protect(msg);
      node = log_msg_alloc_queue_node(msg, path_options);
      iv_list_add_tail(&node->list, &self->input_queues[thread_index].items);

      self->input_queues[thread_index].len++;

      log_msg_unref(msg);
      return;
    }
  // slow path
  _push_tail_single_message(s, msg, path_options);
}

static void
_maybe_move_part_of_input_to_front_cache(LogQueueDiskNonReliable *self, InputQueue *input_queue)
{
  g_mutex_lock(&self->super.super.lock);
  gint num_msgs_to_send_to_front_cache = (qdisk_get_length(self->super.qdisk) == 0 && self->flow_control_window.len == 0)?
                                         MIN(self->front_cache.limit - self->front_cache.len, input_queue->len) : 0;

  if (num_msgs_to_send_to_front_cache == 0)
    goto exit;

  gint count = 0;
  struct iv_list_head *ilh, *ilh2;
  iv_list_for_each_safe(ilh, ilh2, &input_queue->items)
  {
    if (count >= num_msgs_to_send_to_front_cache)
      break;
    LogMessageQueueNode *node = iv_list_entry(ilh, LogMessageQueueNode, list);
    LogPathOptions lpo = LOG_PATH_OPTIONS_INIT;
    LogMessage *msg;
    _extract_queue_node(node, &msg, &lpo);
    _push_tail_front_cache(self, msg, &lpo);
    iv_list_del_init(ilh);
    log_msg_free_queue_node(node);
    input_queue->len--;
    log_queue_queued_messages_inc(&self->super.super);
    count++;
  }
  log_queue_push_notify(&self->super.super);
exit:
  g_mutex_unlock(&self->super.super.lock);
}

static void
_move_part_of_input_to_disk_or_flow_control_window(LogQueueDiskNonReliable *self, InputQueue *input_queue)
{
  LogMessageQueueNode *node = iv_list_entry(input_queue->items.next, LogMessageQueueNode, list);
  LogPathOptions lpo = LOG_PATH_OPTIONS_INIT;
  lpo.flow_control_requested = node->flow_control_requested;

  LogMessage *msg;
  _extract_queue_node(node, &msg, &lpo);
  _push_tail_single_message(&self->super.super, msg, &lpo);

  iv_list_del_init(&node->list);
  input_queue->len--;
  log_msg_free_queue_node(node);
}

static inline gpointer
_move_input(gpointer user_data)
{
  LogQueueDiskNonReliable *self = (LogQueueDiskNonReliable *) user_data;
  gint thread_index = main_loop_worker_get_thread_index();
  g_assert(thread_index >= 0);

  if (self->input_queues[thread_index].len == 0)
    goto exit;

  while (self->input_queues[thread_index].len > 0)
    {
      _maybe_move_part_of_input_to_front_cache(self, &self->input_queues[thread_index]);
      if (self->input_queues[thread_index].len > 0)
        _move_part_of_input_to_disk_or_flow_control_window(self, &self->input_queues[thread_index]);
    }
exit:
  self->input_queues[thread_index].finish_cb_registered = FALSE;
  log_queue_unref(&self->super.super);
  return NULL;
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
log_queue_disk_free_queue(struct iv_list_head *q)
{
  while (!iv_list_empty(q))
    {
      LogMessageQueueNode *node;
      LogPathOptions path_options = LOG_PATH_OPTIONS_INIT;
      LogMessage *msg;

      node = iv_list_entry(q->next, LogMessageQueueNode, list);
      iv_list_del(&node->list);

      path_options.ack_needed = node->ack_needed;
      msg = node->msg;
      log_msg_free_queue_node(node);
      log_msg_ack(msg, &path_options, AT_ABORTED);
      log_msg_unref(msg);
    }
}

static void
_free(LogQueue *s)
{
  LogQueueDiskNonReliable *self = (LogQueueDiskNonReliable *)s;

  for (gint i = 0; i < self->num_input_queues; i++)
    {
      g_assert(self->input_queues[i].finish_cb_registered == FALSE);
      log_queue_disk_free_queue(&self->input_queues[i].items);
    }

  g_assert(self->front_cache_output.len == 0);
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

  // for compatibility reasons we save a single queue for front_cache and front_cache_output
  // move the rest to front_cache_output, saving as a single queue
  if (self->front_cache.len > 0)
    {
      iv_list_splice_tail_init(&self->front_cache.items, &self->front_cache_output.items);
      self->front_cache_output.len += self->front_cache.len;
      self->front_cache.len = 0;
    }

  g_mutex_lock(&s->super.lock);

  for (gint i = 0; i < self->num_input_queues; i++)
    {
      if (self->input_queues[i].len > 0)
        {
          iv_list_splice_tail_init(&self->input_queues[i].items, &self->flow_control_window.items);
          self->flow_control_window.len += self->input_queues[i].len;
          self->input_queues[i].len = 0;
        }
    }
  g_mutex_unlock(&s->super.lock);

  if (qdisk_stop(s->qdisk, _save_queue_callback, self))
    {
      *persistent = TRUE;
      result = TRUE;
    }

  _empty_queue(self, &self->front_cache_output);
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

  gint max_threads = main_loop_worker_get_max_number_of_threads();
  LogQueueDiskNonReliable *self;
  self = g_malloc0(sizeof(LogQueueDiskNonReliable) + max_threads * sizeof(self->input_queues[0]));

  log_queue_disk_init_instance(&self->super, options, "SLQF", filename, persist_name, stats_level,
                               driver_sck_builder, queue_sck_builder);
  _init_memory_queue(&self->front_cache);
  _init_memory_queue(&self->backlog);
  _init_memory_queue(&self->flow_control_window);
  _init_memory_queue(&self->front_cache_output);
  if (options->front_cache_size % 2 == 0)
    {
      self->front_cache.limit = self->front_cache_output.limit = options->front_cache_size / 2;
    }
  else
    {
      self->front_cache_output.limit = (options->front_cache_size - 1) / 2;
      self->front_cache.limit = options->front_cache_size - self->front_cache_output.limit;
    }

  self->num_input_queues = max_threads;
  for (gint i = 0; i < self->num_input_queues; i++)
    {
      INIT_IV_LIST_HEAD(&self->input_queues[i].items);
      worker_batch_callback_init(&self->input_queues[i].cb);
      self->input_queues[i].cb.func = _move_input;
      self->input_queues[i].cb.user_data = self;
    }

  _set_virtual_functions(self);
  return &self->super.super;
}
