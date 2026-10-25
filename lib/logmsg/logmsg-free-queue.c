/*
 * Copyright (c) 2026 Axoflow
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
#include "logmsg-free-queue.h"
#include "logmsg.h"
#include "mainloop-worker.h"
#include "tls-support.h"

#include <iv.h>
#include <iv_event.h>
#include <iv_list.h>

/*
 * There are two threads collaborating here:
 *
 *   - the source thread that ingests messages and pushes them to the
 *     pipeline out.  This one allocates LogMessages.  We call this "origin"
 *     thread below.
 *
 *   - the destination thread that sends messages somewhere and wants to
 *     free LogMessage instances once processed.  We call this the "queueing"
 *     thread.
 *
 */

/* number of messages handed over to the originating thread at once */
#define LOG_MSG_FREE_QUEUE_BATCH_SIZE 64

/* number of batches collected concurrently (for different origins) in a thread */
#define LOG_MSG_FREE_QUEUE_PENDING_BATCHES 8

/*
 * The queue assigned to origin threads: messages that were allocated by
 * this thread and are to be freed.  Messages are linked using an
 * iv_list_head as their LogMessage->free_list member.
 *
 * The lock is taken once per batch by the queueing threads and once per
 * wakeup by their origin, batches are spliced in and out in O(1).
 *
 * The queue is allocated the first time an origin thread with a given id
 * starts up and lives until exit, as thread ids get reused.
 */
typedef struct _LogMessageFreeQueue
{
  GMutex lock;
  struct iv_list_head msgs;
  struct iv_event wakeup;
  gboolean open;
} LogMessageFreeQueue;

typedef struct _LogMessageFreeBatch
{
  guint16 origin_thread_id;
  guint16 num_msgs;
  struct iv_list_head msgs;
} LogMessageFreeBatch;

/* indexed by the (one based) worker thread id, 0 is never used.  This
 * provides access to the free-queue of the "origin" threads */
typedef struct _LogMessageFreeQueueTable LogMessageFreeQueueTable;
struct _LogMessageFreeQueueTable
{
  LogMessageFreeQueueTable *superseded;
  gsize size;
  LogMessageFreeQueue *queues[];
};

static LogMessageFreeQueueTable *queues_table;
static GMutex queues_lock;

TLS_BLOCK_START
{
  /* queueing side state */
  LogMessageFreeBatch pending[LOG_MSG_FREE_QUEUE_PENDING_BATCHES];
  LogMessageFreeBatch *last_batch;
  struct iv_task flush_task;
  guint16 next_pending;
  /* boolean, indicates whether the current thread is a queueing thread */
  guint16 queueing;

  /* origin side state */
  /* 0 if we don't support the free queue */
  guint16 current_id;
}
TLS_BLOCK_END;

#define queueing     __tls_deref(queueing)
#define current_id   __tls_deref(current_id)
#define pending      __tls_deref(pending)
#define next_pending __tls_deref(next_pending)
#define last_batch   __tls_deref(last_batch)
#define flush_task   __tls_deref(flush_task)

static void
_free_list(struct iv_list_head *msgs)
{
  struct iv_list_head *lh, *lh2;

  iv_list_for_each_safe(lh, lh2, msgs)
  {
    LogMessage *msg = iv_list_entry(lh, LogMessage, free_list);

    log_msg_free_in_place(msg);
  }
  INIT_IV_LIST_HEAD(msgs);
}

/* NOTE: runs in the origin thread */
static void
_queue_drain(LogMessageFreeQueue *self)
{
  struct iv_list_head msgs;

  INIT_IV_LIST_HEAD(&msgs);
  g_mutex_lock(&self->lock);
  iv_list_splice_tail_init(&self->msgs, &msgs);
  g_mutex_unlock(&self->lock);

  _free_list(&msgs);
}

static void
_queue_wakeup(gpointer user_data)
{
  LogMessageFreeQueue *self = (LogMessageFreeQueue *) user_data;

  _queue_drain(self);
}

/* queues are created once and are never freed, so the lookup is lock-free */
static inline LogMessageFreeQueue *
_queue_lookup(guint16 id)
{
  LogMessageFreeQueueTable *table = g_atomic_pointer_get(&queues_table);

  if (!table || id >= table->size)
    return NULL;
  return g_atomic_pointer_get(&table->queues[id]);
}

/* the queues lock must be held */
static LogMessageFreeQueueTable *
_queue_table_grow(guint16 id)
{
  LogMessageFreeQueueTable *old = queues_table;
  gsize size = MAX((gsize) id + 1, (gsize) main_loop_worker_get_max_number_of_threads() + 1);
  LogMessageFreeQueueTable *table = g_malloc0(sizeof(*table) + size * sizeof(table->queues[0]));

  table->size = size;
  table->superseded = old;
  if (old)
    memcpy(table->queues, old->queues, old->size * sizeof(table->queues[0]));
  g_atomic_pointer_set(&queues_table, table);
  return table;
}

static void
_queue_free(LogMessageFreeQueue *self)
{
  g_mutex_clear(&self->lock);
  g_free(self);
}

/* runs in the origin thread at startup */
static LogMessageFreeQueue *
_queue_lookup_or_create(guint16 id)
{
  LogMessageFreeQueue *self;

  g_mutex_lock(&queues_lock);
  LogMessageFreeQueueTable *table = queues_table;

  if (!table || id >= table->size)
    table = _queue_table_grow(id);

  self = table->queues[id];
  if (!self)
    {
      self = g_new0(LogMessageFreeQueue, 1);
      g_mutex_init(&self->lock);
      INIT_IV_LIST_HEAD(&self->msgs);
      IV_EVENT_INIT(&self->wakeup);
      self->wakeup.cookie = self;
      self->wakeup.handler = _queue_wakeup;
      g_atomic_pointer_set(&table->queues[id], self);
    }
  g_mutex_unlock(&queues_lock);
  return self;
}

/*
 * Hand over a batch to the originating thread.  If the origin is gone (e.g.
 * queue->open == FALSE), the messages are freed in place.
 */
static void
_batch_hand_over(LogMessageFreeBatch *batch)
{
  LogMessageFreeQueue *queue = _queue_lookup(batch->origin_thread_id);
  gboolean accepted = FALSE;

  if (queue)
    {
      g_mutex_lock(&queue->lock);
      if (queue->open)
        {
          gboolean was_empty = iv_list_empty(&queue->msgs);

          iv_list_splice_tail_init(&batch->msgs, &queue->msgs);
          if (was_empty)
            iv_event_post(&queue->wakeup);
          accepted = TRUE;
        }
      g_mutex_unlock(&queue->lock);
    }

  if (!accepted)
    _free_list(&batch->msgs);
  batch->num_msgs = 0;
}

static LogMessageFreeBatch *
_batch_lookup(guint16 origin_thread_id)
{
  LogMessageFreeBatch *empty = NULL;

  for (guint i = 0; i < LOG_MSG_FREE_QUEUE_PENDING_BATCHES; i++)
    {
      LogMessageFreeBatch *batch = &pending[i];

      if (batch->num_msgs == 0)
        {
          if (!empty)
            empty = batch;
        }
      else if (batch->origin_thread_id == origin_thread_id)
        return batch;
    }

  if (!empty)
    {
      /* all slots are taken by other origins, evict one of them */
      empty = &pending[next_pending];
      next_pending = (next_pending + 1) % LOG_MSG_FREE_QUEUE_PENDING_BATCHES;
      _batch_hand_over(empty);
    }
  empty->origin_thread_id = origin_thread_id;
  return empty;
}

guint16
log_msg_free_queue_get_current_id(void)
{
  return current_id;
}

static void
_flush_pending(void)
{
  for (guint i = 0; i < LOG_MSG_FREE_QUEUE_PENDING_BATCHES; i++)
    {
      if (pending[i].num_msgs > 0)
        _batch_hand_over(&pending[i]);
    }
}

/* runs from the ivykis loop of the queueing thread, once it finished the
 * work (job, task, timer) during which the messages were accumulated into
 * batches.  */
static void
_flush_task(gpointer user_data)
{
  _flush_pending();
}

/*
 * Queue a message to be freed by its originating thread, returns FALSE if
 * the message is to be freed in place by the caller.
 */
gboolean
log_msg_free_queue_push(LogMessage *msg)
{
  guint16 origin_thread_id = msg->origin_thread_id;

  if (origin_thread_id == 0 || !queueing || origin_thread_id == current_id)
    return FALSE;

  LogMessageFreeBatch *batch = last_batch;

  if (!batch || batch->origin_thread_id != origin_thread_id)
    batch = last_batch = _batch_lookup(origin_thread_id);

  iv_list_add_tail(&msg->free_list, &batch->msgs);
  batch->num_msgs++;
  if (batch->num_msgs == LOG_MSG_FREE_QUEUE_BATCH_SIZE)
    _batch_hand_over(batch);
  else if (!iv_task_registered(&flush_task))
    iv_task_register(&flush_task);
  return TRUE;
}

/*
 * Start handing over the messages freed by the current thread, to be
 * called from a worker thread running an ivykis loop.
 */
void
log_msg_free_queue_thread_start(void)
{
  /* queueing side */
  for (guint i = 0; i < LOG_MSG_FREE_QUEUE_PENDING_BATCHES; i++)
    {
      pending[i].num_msgs = 0;
      INIT_IV_LIST_HEAD(&pending[i].msgs);
    }
  next_pending = 0;
  last_batch = NULL;
  IV_TASK_INIT(&flush_task);
  flush_task.handler = _flush_task;
  queueing = TRUE;

  /* origin side */
  gint thread_index = main_loop_worker_get_thread_index();

  if (thread_index < 0)
    return;

  LogMessageFreeQueue *queue = _queue_lookup_or_create(thread_index + 1);

  iv_event_register(&queue->wakeup);
  g_mutex_lock(&queue->lock);
  g_assert(!queue->open);
  queue->open = TRUE;
  g_mutex_unlock(&queue->lock);

  current_id = thread_index + 1;
}

void
log_msg_free_queue_thread_stop(void)
{
  if (!queueing)
    return;

  if (iv_task_registered(&flush_task))
    iv_task_unregister(&flush_task);
  _flush_pending();
  queueing = FALSE;

  if (current_id == 0)
    return;

  LogMessageFreeQueue *queue = _queue_lookup(current_id);

  g_mutex_lock(&queue->lock);
  queue->open = FALSE;
  g_mutex_unlock(&queue->lock);
  _queue_drain(queue);
  iv_event_unregister(&queue->wakeup);

  current_id = 0;
}

void
log_msg_free_queue_global_init(void)
{
}

/* to be called once all worker threads exited, as the lookup is lock-free */
void
log_msg_free_queue_global_deinit(void)
{
  LogMessageFreeQueueTable *table = queues_table;

  queues_table = NULL;
  if (!table)
    return;

  /* the newest table holds every queue, the superseded ones a prefix of it */
  for (gsize i = 0; i < table->size; i++)
    {
      if (table->queues[i])
        _queue_free(table->queues[i]);
    }

  while (table)
    {
      LogMessageFreeQueueTable *superseded = table->superseded;

      g_free(table);
      table = superseded;
    }
}
