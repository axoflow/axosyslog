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

  /* used by threads that do not have a thread index (e.g. the main thread) */
  guint shared_seq;

  gint num_threads;
  LogBalancerThreadState threads[];
};

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

static gint
_round_robin_thread(LogBalancer *self, LogBalancerThreadState *state)
{
  gint target = state->cursor;

  if (++state->run_length >= _get_batch_size(self))
    _thread_state_advance(self, state);
  return target;
}

/* slow path for threads without thread state: a single atomic sequence
 * number shared between all of them */
static gint
_round_robin_shared(LogBalancer *self)
{
  guint seq = (guint) g_atomic_int_add(&self->shared_seq, 1);

  return (seq / _get_batch_size(self)) % self->num_targets;
}

static gint
_round_robin(LogBalancer *self)
{
  LogBalancerThreadState *state = _get_thread_state(self);

  if (state)
    return _round_robin_thread(self, state);
  return _round_robin_shared(self);
}

/* LogBalancer */

/* runs in the source thread */
gint
log_balancer_select(LogBalancer *self, LogMessage *msg)
{
  if (self->num_targets == 1)
    return 0;

  return _round_robin(self);
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
  self->num_targets = num_targets;
  self->num_threads = num_threads;
  _init_thread_states(self);
  return self;
}

void
log_balancer_free(LogBalancer *self)
{
  g_free(self);
}

/* LogBalancerOptions */

void
log_balancer_options_defaults(LogBalancerOptions *options)
{
  options->batch_size = -1;
}

gboolean
log_balancer_options_init(LogBalancerOptions *options, GlobalConfig *cfg)
{
  if (options->batch_size == -1)
    options->batch_size = 1;
  return TRUE;
}

void
log_balancer_options_destroy(LogBalancerOptions *options)
{
}
