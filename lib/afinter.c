/*
 * Copyright (c) 2002-2012 Balabit
 * Copyright (c) 1998-2012 Balázs Scheidler
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#include "afinter.h"
#include "logreader.h"
#include "stats/stats-registry.h"
#include "stats/stats-cluster-logpipe.h"
#include "stats/stats-cluster-single.h"
#include "messages.h"
#include "apphook.h"
#include "mainloop-threaded-worker.h"

#include <iv_event.h>

typedef struct _AFInterSource AFInterSource;

static GMutex internal_msg_lock;
static GQueue *internal_msg_queue;
static AFInterSource *current_internal_source;

static AFInterMetrics metrics;

/* the expiration timer of the next MARK message */
static struct timespec next_mark_target = { -1, 0 };
/* as different sources from different threads can call afinter_postpone_mark,
   and we use the value in the init thread, we need to synchronize the value references
*/
static GMutex internal_mark_target_lock;

/*
 * This class is parallel to LogReader, e.g. it hangs on the
 * AFInterSourceDriver on the left (e.g.  feeds messages to
 * AFInterSourceDriver).
 *
 * Threading:
 * ==========
 *
 * syslog-ng can generate internal() messages in any of its threads, thus
 * some care must be taken to make the internal() source multithreaded.
 * This is how it works:
 *
 * Whenever a thread decides to send a message using the msg_() API, it puts
 * an entry into the internal_msg_queue under the protection of
 * "internal_msg_lock".
 *
 * The receiving side of this queue is in the main thread, where the
 * internal() source is operating.  This object will publish a pointer to
 * itself into current_internal_source.  This pointer will be set under the
 * protection of the internal_msg_lock.  The internal source will define an
 * ivykis event, a post is submitted to this event whenever a new message is
 * added to the queue.
 *
 * Once the event arrives to the main loop, it wakes up and feeds all
 * internal messages into the log path.
 *
 * If the window is depleted (e.g. flow control is enabled and the
 * destination is unable to process any more messages),
 * free_to_send will be set to FALSE, which means that messages
 * will be added to the queue, but the wakeup will not be done.
 *
 * When the window becomes free, log_source_wakeup() is called, which
 * restores the free_to_send flag (e.g.  further messages will
 * wake up the source) and also starts emptying the messages accumulated in the queue.
 *
 * Possible races:
 * ===============
 *
 * The biggest offenders are when a client thread submits a message and
 * we're in the process of getting asleep (no window), or waking up.  There
 * are notes in the code how we handle these cases (search for "Possible race").
 *
 */
struct _AFInterSource
{
  LogSource super;
  MainLoopThreadedWorker thread;
  gint mark_freq;
  const AFInterSourceOptions *options;
  struct iv_event post;
  struct iv_event schedule_wakeup;
  struct iv_event exit;
  struct iv_timer mark_timer;
  struct iv_task restart_task;
  guint watches_running:1, free_to_send:1;
};

static void afinter_source_update_watches(AFInterSource *self);
void afinter_message_posted(LogMessage *msg);

static void
afinter_source_post(gpointer s)
{
  AFInterSource *self = (AFInterSource *) s;
  LogMessage *msg;

  while (log_source_free_to_send(&self->super))
    {
      g_mutex_lock(&internal_msg_lock);
      msg = g_queue_pop_head(internal_msg_queue);
      g_mutex_unlock(&internal_msg_lock);
      if (!msg)
        break;

      stats_counter_dec(metrics.queued);
      stats_counter_inc(metrics.processed);
      log_source_post(&self->super, msg);
      main_loop_worker_invoke_batch_callbacks();
    }
  afinter_source_update_watches(self);
}

static void afinter_source_start_watches(AFInterSource *self);
static void afinter_source_stop_watches(AFInterSource *self);

static gboolean
afinter_source_thread_init(MainLoopThreadedWorker *s)
{
  AFInterSource *self = (AFInterSource *) s->data;

  /* post event is used by other threads and can only be unregistered if
   * current_afinter_source is set to NULL in a thread safe manner */
  iv_event_register(&self->post);
  iv_event_register(&self->schedule_wakeup);
  iv_event_register(&self->exit);
  return TRUE;
}

static void
afinter_source_thread_deinit(MainLoopThreadedWorker *s)
{
  AFInterSource *self = (AFInterSource *) s->data;

  iv_event_unregister(&self->exit);
  iv_event_unregister(&self->post);
  iv_event_unregister(&self->schedule_wakeup);
}

static void
afinter_source_run(MainLoopThreadedWorker *s)
{
  AFInterSource *self = (AFInterSource *) s->data;

  g_mutex_lock(&internal_msg_lock);
  self->free_to_send = TRUE;
  g_mutex_unlock(&internal_msg_lock);

  afinter_source_start_watches(self);
  afinter_source_update_watches(self);

  iv_main();

  g_mutex_lock(&internal_msg_lock);
  current_internal_source = NULL;
  g_mutex_unlock(&internal_msg_lock);

  afinter_source_stop_watches(self);
}

static void
afinter_source_thread_request_exit(MainLoopThreadedWorker *s)
{
  AFInterSource *self = (AFInterSource *) s->data;

  iv_event_post(&self->exit);
}

static gboolean
afinter_source_start_thread(LogSource *s)
{
  AFInterSource *self = (AFInterSource *) s;

  main_loop_threaded_worker_start(&self->thread);
  return TRUE;
}

static void
afinter_source_mark(gpointer s)
{
  AFInterSource *self = (AFInterSource *) s;
  struct timespec nmt;

  g_mutex_lock(&internal_mark_target_lock);
  nmt = next_mark_target;
  g_mutex_unlock(&internal_mark_target_lock);

  if (nmt.tv_sec <= self->mark_timer.expires.tv_sec)
    {
      /* the internal_mark_target has not been overwritten by an incoming message in afinter_postpone_mark
         (there was no msg in the meantime) -> the mark msg can be sent */
      afinter_message_posted(log_msg_new_mark());

      /* the next_mark_target will be increased in afinter_postpone_mark */
    }
}

static void
afinter_source_wakeup(LogSource *s)
{
  AFInterSource *self = (AFInterSource *) s;

  /*
   * We might get called even after this AFInterSource has been
   * deinitialized, in which case we must not do anything (since the
   * iv_event triggered here is not registered).
   *
   * This happens when log_writer_deinit() flushes its output queue
   * after the internal source which produced the message has already been
   * deinited. Since init/deinit calls are made in the main thread, no
   * locking is needed.
   *
   */
  if (self->super.super.flags & PIF_INITIALIZED)
    iv_event_post(&self->schedule_wakeup);
}

static void
afinter_source_init_watches(AFInterSource *self)
{
  IV_EVENT_INIT(&self->post);
  self->post.cookie = self;
  self->post.handler = afinter_source_post;
  IV_TIMER_INIT(&self->mark_timer);
  self->mark_timer.cookie = self;
  self->mark_timer.handler = afinter_source_mark;
  IV_EVENT_INIT(&self->schedule_wakeup);
  self->schedule_wakeup.cookie = self;
  self->schedule_wakeup.handler = (void (*)(void *)) afinter_source_update_watches;
  IV_EVENT_INIT(&self->exit);
  self->exit.cookie = NULL;
  self->exit.handler = (void (*)(void *)) iv_quit;
  IV_TASK_INIT(&self->restart_task);
  self->restart_task.cookie = self;
  self->restart_task.handler = afinter_source_post;
}

static void
afinter_source_start_watches(AFInterSource *self)
{
  if (!self->watches_running)
    {
      if (self->mark_timer.expires.tv_sec >= 0)
        iv_timer_register(&self->mark_timer);
      self->watches_running = TRUE;
    }
}

static void
afinter_source_stop_watches(AFInterSource *self)
{
  if (self->watches_running)
    {
      if (iv_task_registered(&self->restart_task))
        iv_task_unregister(&self->restart_task);
      if (iv_timer_registered(&self->mark_timer))
        iv_timer_unregister(&self->mark_timer);
      self->watches_running = FALSE;
    }
}

static void
afinter_source_update_watches(AFInterSource *self)
{
  if (!log_source_free_to_send(&self->super))
    {
      /* ok, we go to sleep now. let's disable the post event.
       * Messages get accumulated into internal_msg_queue.  */
      g_mutex_lock(&internal_msg_lock);
      self->free_to_send = FALSE;
      g_mutex_unlock(&internal_msg_lock);

      /* Possible race:
       *
       * After the check log_source_free_to_send() above, the destination
       * may actually write out a message, thus by the time we get here, the
       * window may have space again.  This is taken care of by the fact
       * that the wakeup is running in the main thread, which we do too.  So
       * the wakeup is either completely performed before we entered this
       * function, or after we return.
       *
       * In case it happened earlier, the check above will find that we have
       * window space, in case it's going to be happening afterwards, we
       * will be woken up by the schedule_wakeup event (which calls
       * update_watches again).
       */

      /* MARK events also get disabled */
      afinter_source_stop_watches(self);
    }
  else
    {
      /* ok we can send our stuff. make sure we wake up */
      afinter_source_stop_watches(self);
      self->mark_timer.expires = next_mark_target;
      afinter_source_start_watches(self);

      /* Possible race:
       *
       * The free_to_send flag is set to FALSE here (in case
       * we're just waking up).  In case the sender submits a message, it'll
       * not trigger the self->post (since free_to_send is FALSE).  This is
       * taken care of by the queue-length check in the locked region below.
       * If the queue has elements, we need to wake up, because we may have
       * lost a wakeup call.  If it happens after the locked region, that
       * doesn't matter, in that case we already set
       * free_to_send = TRUE to ourselves, thus the post event will also
       * be triggered.
       */

      g_mutex_lock(&internal_msg_lock);
      if (internal_msg_queue && g_queue_get_length(internal_msg_queue) > 0)
        iv_task_register(&self->restart_task);
      self->free_to_send = TRUE;
      g_mutex_unlock(&internal_msg_lock);
    }
}

static gboolean
afinter_source_init(LogPipe *s)
{
  AFInterSource *self = (AFInterSource *) s;
  GlobalConfig *cfg = log_pipe_get_config(s);

  if (!log_source_init(s))
    return FALSE;

  self->mark_freq = cfg->mark_freq;
  afinter_postpone_mark(self->mark_freq);
  self->mark_timer.expires = next_mark_target;

  g_mutex_lock(&internal_msg_lock);
  current_internal_source = self;
  stats_counter_set(metrics.queue_capacity, self->options->queue_capacity);
  g_mutex_unlock(&internal_msg_lock);

  return TRUE;
}

static gboolean
afinter_source_deinit(LogPipe *s)
{
  AFInterSource *self = (AFInterSource *) s;

  g_mutex_lock(&internal_msg_lock);
  current_internal_source = NULL;
  g_mutex_unlock(&internal_msg_lock);

  return log_source_deinit(&self->super.super);
}

static void
afinter_source_free(LogPipe *s)
{
  AFInterSource *self = (AFInterSource *) s;

  main_loop_threaded_worker_clear(&self->thread);
  log_source_free(s);
}

static LogSource *
afinter_source_new(AFInterSourceDriver *owner, AFInterSourceOptions *options)
{
  AFInterSource *self = g_new0(AFInterSource, 1);

  log_source_init_instance(&self->super, owner->super.super.super.cfg);
  log_source_set_options(&self->super, &options->super, owner->super.super.id, NULL, FALSE,
                         owner->super.super.super.expr_node);
  main_loop_threaded_worker_init(&self->thread, MLW_THREADED_INPUT_WORKER, self);
  self->thread.thread_init = afinter_source_thread_init;
  self->thread.thread_deinit = afinter_source_thread_deinit;
  self->thread.run = afinter_source_run;
  self->thread.request_exit = afinter_source_thread_request_exit;

  afinter_source_init_watches(self);
  self->super.super.init = afinter_source_init;
  self->super.super.deinit = afinter_source_deinit;
  self->super.wakeup = afinter_source_wakeup;
  self->super.super.free_fn = afinter_source_free;

  self->options = options;

  return &self->super;
}


void
afinter_source_options_defaults(AFInterSourceOptions *options)
{
  log_source_options_defaults(&options->super);
  options->queue_capacity = 10000;
}

static gboolean
afinter_sd_pre_config_init(LogPipe *s)
{
  main_loop_worker_allocate_thread_space(1);
  return log_pipe_pre_config_init_method(s);
}

static gboolean
afinter_sd_init(LogPipe *s)
{
  AFInterSourceDriver *self = (AFInterSourceDriver *) s;
  GlobalConfig *cfg = log_pipe_get_config(s);

  if (cfg_is_config_version_older(cfg, VERSION_VALUE_3_29))
    {
      msg_warning_once("WARNING: The internal_queue_length stat counter has been renamed to internal_source.queued",
                       cfg_format_config_version_tag(cfg));
    }

  if (!log_src_driver_init_method(s))
    return FALSE;

  if (current_internal_source != NULL)
    {
      msg_error("Multiple internal() sources were detected, this is not possible");
      return FALSE;
    }

  log_source_options_init(&self->source_options.super, cfg, self->super.super.group);
  self->source_options.super.stats_level = STATS_LEVEL0;
  self->source_options.super.stats_source = stats_register_type("internal");
  self->source = afinter_source_new(self, &self->source_options);
  log_pipe_append(&self->source->super, s);

  if (!log_pipe_init(&self->source->super))
    {
      log_pipe_unref(&self->source->super);
      self->source = NULL;
      return FALSE;
    }

  return TRUE;
}

static gboolean
afinter_sd_post_config_init(LogPipe *s)
{
  AFInterSourceDriver *self = (AFInterSourceDriver *) s;

  if (!afinter_source_start_thread(self->source))
    return FALSE;

  return log_pipe_post_config_init_method(s);
}

static gboolean
afinter_sd_deinit(LogPipe *s)
{
  AFInterSourceDriver *self = (AFInterSourceDriver *) s;

  if (self->source)
    {
      log_pipe_deinit(&self->source->super);
      /* break circular reference created during _init */
      log_pipe_unref(&self->source->super);
      self->source = NULL;
    }

  if (!log_src_driver_deinit_method(s))
    return FALSE;

  return TRUE;
}

static void
afinter_sd_free(LogPipe *s)
{
  AFInterSourceDriver *self = (AFInterSourceDriver *) s;

  g_assert(!self->source);
  log_src_driver_free(s);
}


LogDriver *
afinter_sd_new(GlobalConfig *cfg)
{
  AFInterSourceDriver *self = g_new0(AFInterSourceDriver, 1);

  log_src_driver_init_instance((LogSrcDriver *)&self->super, cfg);
  self->super.super.super.pre_config_init = afinter_sd_pre_config_init;
  self->super.super.super.init = afinter_sd_init;
  self->super.super.super.deinit = afinter_sd_deinit;
  self->super.super.super.free_fn = afinter_sd_free;
  self->super.super.super.post_config_init = afinter_sd_post_config_init;

  afinter_source_options_defaults(&self->source_options);

  return (LogDriver *)&self->super.super;
}

/****************************************************************************
 * Global entry points, without an AFInterSourceDriver instance.
 ****************************************************************************/

void
afinter_postpone_mark(gint mark_freq)
{
  if (mark_freq > 0)
    {
      iv_validate_now();
      g_mutex_lock(&internal_mark_target_lock);
      next_mark_target = iv_now;
      next_mark_target.tv_sec += mark_freq;
      g_mutex_unlock(&internal_mark_target_lock);
    }
  else
    {
      g_mutex_lock(&internal_mark_target_lock);
      next_mark_target.tv_sec = -1;
      g_mutex_unlock(&internal_mark_target_lock);
    }
}

static void
_release_internal_msg_queue(void)
{
  LogMessage *internal_message = g_queue_pop_head(internal_msg_queue);
  while (internal_message)
    {
      stats_counter_dec(metrics.queued);
      log_msg_unref(internal_message);

      internal_message = g_queue_pop_head(internal_msg_queue);
    }
  g_queue_free(internal_msg_queue);
  internal_msg_queue = NULL;
}

void
afinter_message_posted(LogMessage *msg)
{
  g_mutex_lock(&internal_msg_lock);
  if (!current_internal_source)
    {
      if (internal_msg_queue)
        {
          _release_internal_msg_queue();
        }
      log_msg_unref(msg);
      goto exit;
    }

  if (!internal_msg_queue)
    {
      internal_msg_queue = g_queue_new();

      stats_lock();
      StatsClusterKey sc_key;
      stats_cluster_logpipe_key_set(&sc_key, "internal_events_total", NULL, 0);
      stats_cluster_logpipe_key_add_legacy_alias(&sc_key, SCS_GLOBAL, "internal_source", NULL );
      stats_register_counter(0, &sc_key, SC_TYPE_QUEUED, &metrics.queued);
      stats_register_counter(0, &sc_key, SC_TYPE_DROPPED, &metrics.dropped);
      stats_register_counter(0, &sc_key, SC_TYPE_PROCESSED, &metrics.processed);

      stats_cluster_single_key_set(&sc_key, "internal_events_queue_capacity", NULL, 0);
      stats_register_counter(0, &sc_key, SC_TYPE_SINGLE_VALUE, &metrics.queue_capacity);
      stats_unlock();

      stats_counter_set(metrics.queue_capacity, current_internal_source->options->queue_capacity);
    }

  if (g_queue_get_length(internal_msg_queue) >= current_internal_source->options->queue_capacity)
    {
      stats_counter_inc(metrics.dropped);
      log_msg_unref(msg);
      goto exit;
    }

  g_queue_push_tail(internal_msg_queue, msg);
  stats_counter_inc(metrics.queued);

  if (current_internal_source->free_to_send)
    iv_event_post(&current_internal_source->post);
exit:
  g_mutex_unlock(&internal_msg_lock);
}

static void
afinter_register_posted_hook(gint hook_type, gpointer user_data)
{
  msg_set_post_func(afinter_message_posted);
}

void
afinter_global_init(void)
{
  register_application_hook(AH_CONFIG_CHANGED, afinter_register_posted_hook, NULL, AHM_RUN_ONCE);
}

void
afinter_global_deinit(void)
{
  if (internal_msg_queue)
    {
      stats_lock();
      StatsClusterKey sc_key;
      stats_cluster_logpipe_key_set(&sc_key, "internal_events_total", NULL, 0);
      stats_cluster_logpipe_key_add_legacy_alias(&sc_key, SCS_GLOBAL, "internal_source", NULL );
      stats_unregister_counter(&sc_key, SC_TYPE_QUEUED, &metrics.queued);
      stats_unregister_counter(&sc_key, SC_TYPE_DROPPED, &metrics.dropped);
      stats_unregister_counter(&sc_key, SC_TYPE_PROCESSED, &metrics.processed);

      stats_cluster_single_key_set(&sc_key, "internal_events_queue_capacity", NULL, 0);
      stats_unregister_counter(&sc_key, SC_TYPE_SINGLE_VALUE, &metrics.queue_capacity);
      stats_unlock();

      g_queue_free_full(internal_msg_queue, (GDestroyNotify)log_msg_unref);
      internal_msg_queue = NULL;
    }
  current_internal_source = NULL;
}

AFInterMetrics
afinter_get_metrics(void)
{
  return metrics;
}
