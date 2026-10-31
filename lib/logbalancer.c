/*
 * Copyright (c) 2026 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#include "logbalancer.h"
#include "mainloop-worker.h"
#include "template/eval.h"
#include "scratch-buffers.h"
#include "str-format.h"

#define LOG_BALANCER_CACHELINE_SIZE 64

/* Round-robin state of a single input thread, padded to a cache line so
 * that threads do not invalidate each other's state. */
typedef union _LogBalancerThreadState
{
  struct
  {
    /* the target we are currently sending to */
    gint cursor;
    /* the number of messages sent to cursor so far */
    gint run_length;
  };
  gchar __padding[LOG_BALANCER_CACHELINE_SIZE];
} LogBalancerThreadState;

struct _LogBalancer
{
  LogBalancerOptions options;
  gint num_targets;

  LogBalancerIsAvailableFunc is_available;
  gpointer is_available_data;

  /* used by threads that do not have a thread index (e.g. the main thread) */
  guint shared_seq;

  gint num_threads;
  LogBalancerThreadState threads[];
};

static inline gboolean
_is_available(LogBalancer *self, gint target, gboolean available_only)
{
  if (!available_only || !self->is_available)
    return TRUE;
  return self->is_available(target, self->is_available_data);
}

/* round-robin */

static inline gint
_get_batch_size(LogBalancer *self)
{
  return MAX(self->options.batch_size, 1);
}

static inline LogBalancerThreadState *
_get_thread_state(LogBalancer *self)
{
  gint thread_index = main_loop_worker_get_thread_index();

  if (thread_index < 0 || thread_index >= self->num_threads)
    return NULL;
  return &self->threads[thread_index];
}

static inline void
_thread_state_advance(LogBalancer *self, LogBalancerThreadState *state)
{
  state->cursor = (state->cursor + 1) % self->num_targets;
  state->run_length = 0;
}

/* unavailable targets are skipped, which also starts a new run on the
 * next target */
static gint
_round_robin_thread(LogBalancer *self, LogBalancerThreadState *state, gboolean available_only)
{
  for (gint i = 0; i < self->num_targets; i++)
    {
      gint target = state->cursor;

      if (_is_available(self, target, available_only))
        {
          if (++state->run_length >= _get_batch_size(self))
            _thread_state_advance(self, state);
          return target;
        }
      _thread_state_advance(self, state);
    }
  return -1;
}

/* slow path for threads without thread state: a single atomic sequence
 * number shared between all of them */
static gint
_round_robin_shared(LogBalancer *self, gboolean available_only)
{
  guint seq = (guint) g_atomic_int_add(&self->shared_seq, 1);
  gint first = (seq / _get_batch_size(self)) % self->num_targets;

  for (gint i = 0; i < self->num_targets; i++)
    {
      gint target = (first + i) % self->num_targets;

      if (_is_available(self, target, available_only))
        return target;
    }
  return -1;
}

static gint
_round_robin(LogBalancer *self, gboolean available_only)
{
  LogBalancerThreadState *state = _get_thread_state(self);

  if (state)
    return _round_robin_thread(self, state, available_only);
  return _round_robin_shared(self, available_only);
}

/* sticky */

static const gchar *
_eval_buckets(LogBalancer *self, LogMessage *msg, LogTemplateEvalOptions *eval_options, gssize *len)
{
  if (log_template_is_trivial(self->options.partition_buckets))
    return log_template_get_trivial_value(self->options.partition_buckets, msg, len);

  GString *r = scratch_buffers_alloc();
  log_template_format(self->options.partition_buckets, msg, eval_options, r);
  *len = r->len;
  return r->str;
}

/* the bucket of the message, based on its receipt time within the second */
static gint
_calculate_bucket(LogBalancer *self, LogMessage *msg, LogTemplateEvalOptions *eval_options)
{
  gint buckets = 0;
  gssize buckets_len;
  const gchar *buckets_string = _eval_buckets(self, msg, eval_options, &buckets_len);

  if (buckets_len > 0)
    {
      const gchar *p = buckets_string;
      gint p_len = buckets_len;

      if (!scan_positive_int(&p, &p_len, p_len, &buckets))
        {
          msg_warning_once("WARNING: value for worker-partition-buckets() is not an integer, partition bucketing is disabled",
                           evt_tag_str("value", buckets_string));
          buckets = 0;
        }
    }

  if (buckets <= 0)
    return 0;

  return (gint64) msg->timestamps[LM_TS_RECVD].ut_usec * buckets / 1000000LL;
}

static gint
_sticky(LogBalancer *self, LogMessage *msg)
{
  LogTemplateEvalOptions eval_options = DEFAULT_TEMPLATE_EVAL_OPTIONS;
  ScratchBuffersMarker mark;

  scratch_buffers_mark(&mark);

  const gchar *key = log_template_format_tmpbuf(self->options.partition_key, msg, &eval_options, NULL);
  guint hash = g_str_hash(key);

  gint bucket = 0;
  if (self->options.partition_buckets)
    bucket = _calculate_bucket(self, msg, &eval_options);

  scratch_buffers_reclaim_marked(mark);

  return (hash + bucket) % self->num_targets;
}

/* LogBalancer */

/* sticky selection never moves a message to another target, as that would
 * break the ordering of its key */
static inline gint
_fixed_target(LogBalancer *self, gint target, gboolean available_only)
{
  return _is_available(self, target, available_only) ? target : -1;
}

/* runs in the source thread */
gint
log_balancer_select(LogBalancer *self, LogMessage *msg, gboolean available_only)
{
  if (self->num_targets == 1)
    return _fixed_target(self, 0, available_only);

  if (self->options.partition_key)
    return _fixed_target(self, _sticky(self, msg), available_only);

  return _round_robin(self, available_only);
}

void
log_balancer_set_availability_check(LogBalancer *self, LogBalancerIsAvailableFunc is_available,
                                    gpointer user_data)
{
  self->is_available = is_available;
  self->is_available_data = user_data;
}

gint
log_balancer_get_num_targets(LogBalancer *self)
{
  return self->num_targets;
}

static void
_init_thread_states(LogBalancer *self)
{
  for (gint i = 0; i < self->num_threads; i++)
    {
      LogBalancerThreadState *state = &self->threads[i];

      /* start threads on different targets so that they do not send
       * their first messages to the same one */
      state->cursor = i % self->num_targets;
      state->run_length = 0;
    }
}

LogBalancer *
log_balancer_new(const LogBalancerOptions *options, gint num_targets)
{
  g_assert(num_targets > 0);

  gint num_threads = main_loop_worker_get_max_number_of_threads();
  LogBalancer *self = g_malloc0(sizeof(LogBalancer) + num_threads * sizeof(LogBalancerThreadState));

  self->options = *options;
  self->options.partition_key = log_template_ref(options->partition_key);
  self->options.partition_buckets = log_template_ref(options->partition_buckets);
  self->num_targets = num_targets;
  self->num_threads = num_threads;
  _init_thread_states(self);
  return self;
}

void
log_balancer_free(LogBalancer *self)
{
  log_template_unref(self->options.partition_key);
  log_template_unref(self->options.partition_buckets);
  g_free(self);
}

/* LogBalancerOptions */

void
log_balancer_options_set_partition_key_ref(LogBalancerOptions *options, LogTemplate *partition_key)
{
  log_template_unref(options->partition_key);
  options->partition_key = partition_key;
}

void
log_balancer_options_set_partition_buckets_ref(LogBalancerOptions *options, LogTemplate *partition_buckets)
{
  log_template_unref(options->partition_buckets);
  options->partition_buckets = partition_buckets;
}

void
log_balancer_options_defaults(LogBalancerOptions *options)
{
  options->batch_size = -1;
  options->partition_key = NULL;
  options->partition_buckets = NULL;
}

gboolean
log_balancer_options_init(LogBalancerOptions *options, GlobalConfig *cfg)
{
  if (options->batch_size == -1)
    options->batch_size = 1;

  if (options->partition_key && log_template_is_literal_string(options->partition_key))
    {
      msg_error("worker-partition-key() should not be literal string, use macros to form proper partitions");
      return FALSE;
    }
  return TRUE;
}

void
log_balancer_options_destroy(LogBalancerOptions *options)
{
  log_template_unref(options->partition_key);
  options->partition_key = NULL;
  log_template_unref(options->partition_buckets);
  options->partition_buckets = NULL;
}
