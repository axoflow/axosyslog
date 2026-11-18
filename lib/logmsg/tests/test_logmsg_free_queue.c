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
#include <criterion/criterion.h>
#include <criterion/new/assert.h>

#include "logmsg/logmsg.h"
#include "logmsg/logmsg-free-queue.h"
#include "logpipe.h"
#include "mainloop-worker.h"
#include "apphook.h"
#include "cfg.h"
#include "stats/stats-registry.h"
#include "stats/stats-cluster-single.h"

#include <iv.h>

/*
 * The test thread is the "queueing" thread that drops the last reference
 * of messages.  Origin threads allocate messages on request and only drain
 * their free queue when stopped, so whatever was handed over to them stays
 * allocated until then.
 *
 * Leaks show up in the allocated bytes gauge, double frees under ASan.
 */

#define MAX_ORIGINS 9

typedef struct _OriginThread
{
  GThread *thread;
  GMutex lock;
  GCond cond;
  void (*func)(gpointer user_data);
  gpointer user_data;
  gboolean stop;
} OriginThread;

static gpointer
_origin_thread_main(gpointer user_data)
{
  OriginThread *self = user_data;

  iv_init();
  main_loop_worker_thread_start(MLW_ASYNC_WORKER);
  log_msg_free_queue_thread_start();

  g_mutex_lock(&self->lock);
  while (!self->stop)
    {
      if (self->func)
        {
          self->func(self->user_data);
          self->func = NULL;
          g_cond_broadcast(&self->cond);
        }
      else
        {
          g_cond_wait(&self->cond, &self->lock);
        }
    }
  g_mutex_unlock(&self->lock);

  log_msg_free_queue_thread_stop();
  main_loop_worker_thread_stop();
  iv_deinit();
  return NULL;
}

static OriginThread *
origin_thread_new(void)
{
  OriginThread *self = g_new0(OriginThread, 1);

  g_mutex_init(&self->lock);
  g_cond_init(&self->cond);
  self->thread = g_thread_new("origin", _origin_thread_main, self);
  return self;
}

static void
origin_thread_call(OriginThread *self, void (*func)(gpointer user_data), gpointer user_data)
{
  g_mutex_lock(&self->lock);
  self->func = func;
  self->user_data = user_data;
  g_cond_broadcast(&self->cond);
  while (self->func)
    g_cond_wait(&self->cond, &self->lock);
  g_mutex_unlock(&self->lock);
}

/* stops the thread, which frees the messages handed over to it */
static void
origin_thread_free(OriginThread *self)
{
  g_mutex_lock(&self->lock);
  self->stop = TRUE;
  g_cond_broadcast(&self->cond);
  g_mutex_unlock(&self->lock);

  g_thread_join(self->thread);
  g_cond_clear(&self->cond);
  g_mutex_clear(&self->lock);
  g_free(self);
}

static void
_new_message(gpointer user_data)
{
  LogMessage **pmsg = user_data;

  *pmsg = log_msg_new_empty();
}

/* replace *pmsg with a clone, the clone holds the only reference to the original */
static void
_clone_message(gpointer user_data)
{
  LogMessage **pmsg = user_data;
  LogPathOptions path_options = LOG_PATH_OPTIONS_INIT;
  LogMessage *clone = log_msg_clone_cow(*pmsg, &path_options);

  log_msg_unref(*pmsg);
  *pmsg = clone;
}

static LogMessage *
_new_message_in(OriginThread *origin)
{
  LogMessage *msg;

  origin_thread_call(origin, _new_message, &msg);
  return msg;
}

/* the original is allocated by original_origin, the clone by clone_origin */
static LogMessage *
_new_clone_in(OriginThread *clone_origin, OriginThread *original_origin)
{
  LogMessage *msg = _new_message_in(original_origin);

  origin_thread_call(clone_origin, _clone_message, &msg);
  cr_assert(not(zero(ptr, msg->original)));
  return msg;
}

/* hand over (or free) whatever the test thread has batched */
static void
_flush_test_thread(void)
{
  log_msg_free_queue_thread_stop();
}

static StatsCounterItem *allocated_bytes;

static gsize
_allocated_bytes(void)
{
  return stats_counter_get(allocated_bytes);
}

Test(logmsg_free_queue, clone_in_a_batch_rejected_by_a_stopped_origin)
{
  gsize baseline = _allocated_bytes();
  OriginThread *origin = origin_thread_new();
  LogMessage *clone = _new_clone_in(origin, origin);

  origin_thread_free(origin);

  /* both the clone and its original go back to the stopped origin, which
   * rejects the batch, so they are freed by the test thread */
  log_msg_unref(clone);
  _flush_test_thread();

  cr_assert(eq(sz, _allocated_bytes(), baseline));
}

Test(logmsg_free_queue, clone_in_an_evicted_batch)
{
  gsize baseline = _allocated_bytes();
  OriginThread *origins[MAX_ORIGINS];
  LogMessage *msgs[MAX_ORIGINS - 1];

  for (gint i = 0; i < MAX_ORIGINS; i++)
    origins[i] = origin_thread_new();

  /* one message per batch slot: the clone comes from an origin that gets
   * stopped and its original from the one origin without a slot, so
   * freeing the clone in the rejected batch would need a slot while they
   * are all taken, evicting the batch being freed */
  msgs[0] = _new_clone_in(origins[0], origins[MAX_ORIGINS - 1]);
  for (gint i = 1; i < MAX_ORIGINS - 1; i++)
    msgs[i] = _new_message_in(origins[i]);

  origin_thread_free(origins[0]);
  for (gint i = 0; i < MAX_ORIGINS - 1; i++)
    log_msg_unref(msgs[i]);
  _flush_test_thread();

  for (gint i = 1; i < MAX_ORIGINS; i++)
    origin_thread_free(origins[i]);
  cr_assert(eq(sz, _allocated_bytes(), baseline));
}

Test(logmsg_free_queue, clone_in_a_batch_flushed_at_thread_stop)
{
  gsize baseline = _allocated_bytes();
  OriginThread *origin = origin_thread_new();
  OriginThread *clone_origin = origin_thread_new();
  OriginThread *original_origin = origin_thread_new();
  LogMessage *msg = _new_message_in(origin);
  LogMessage *clone = _new_clone_in(clone_origin, original_origin);

  origin_thread_free(clone_origin);

  /* the batch of the stopped origin is flushed second, once the first
   * slot is free again: the original must not end up in a slot that was
   * already flushed */
  log_msg_unref(msg);
  log_msg_unref(clone);
  _flush_test_thread();

  origin_thread_free(origin);
  origin_thread_free(original_origin);
  cr_assert(eq(sz, _allocated_bytes(), baseline));
}

static void
setup(void)
{
  app_startup();
  configuration = cfg_new_snippet();
  configuration->stats_options.level = STATS_LEVEL3;
  stats_reinit(&configuration->stats_options);
  app_running();

  StatsClusterKey sc_key;
  stats_cluster_single_key_set(&sc_key, METRIC(events_allocated_bytes), NULL, 0);
  stats_cluster_single_key_add_legacy_alias(&sc_key, SCS_GLOBAL, "msg_allocated_bytes", NULL);
  stats_lock();
  allocated_bytes = stats_get_counter(&sc_key, SC_TYPE_SINGLE_VALUE);
  stats_unlock();
  cr_assert(not(zero(ptr, allocated_bytes)));

  main_loop_worker_allocate_thread_space(MAX_ORIGINS);
  main_loop_worker_finalize_thread_space();

  /* the test thread has no worker id, so it only frees messages, it never
   * receives any */
  log_msg_free_queue_thread_start();
}

static void
teardown(void)
{
  log_msg_free_queue_thread_stop();

  cfg_free(configuration);
  configuration = NULL;
  app_shutdown();
}

TestSuite(logmsg_free_queue, .init = setup, .fini = teardown);
