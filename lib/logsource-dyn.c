/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2002-2011 Balabit
 * Copyright (c) 2018-2026 László Várady
 * Copyright (c) 2018-2020 Laszlo Budai
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

#include "syslog-ng.h"
#include "logsource.h"
#include "mainloop.h"
#include "dynamic-window.h"
#include "dynamic-window-pool.h"

/*
 * Dynamic window
 *
 * The dynamic window mechanism is operated periodically by the
 * log_source_dynamic_window_realloc() call, which runs in the main thread
 * in a time frame when the source thread is idle. This means that window_size
 * may be increased by destination threads concurrently, but it will never be
 * decreased.
 *
 * request() and release() are the two operations possible; as a result,
 * the LogSource instance either acquires additional dynamic window space
 * or releases some of it, respectively.
 *
 * - request():
 *   The available window of the LogSource increases.
 *   This can be done instantly.
 *
 * - release():
 *   The available window of the LogSource decreases.
 *   This may require multiple steps:
 *     - If the window to be released is currently free, it can be released
 *       instantly.
 *     - If part of the window to be released is still in use, a reclamation
 *       request must be initiated.
 *
 * Window reclamation process:
 * - request_dynamic_window_reclamation():
 *   Initiates a reclamation request for the amount of window that needs to be
 *   released.
 *
 * - gather_dynamic_window_reclamation():
 *   ACKs arriving from the destination side are atomically accumulated
 *   in a pending area.
 *
 * - release_reclaimed_dynamic_window():
 *   The accumulated pending area is finally released in the main thread.
 *
 * DynamicWindow and DynamicWindowPool are not thread-safe, they should only
 * be used from the main thread.
 */

static inline gsize
_window_size_add(LogSource *self, gsize value, gboolean *suspended)
{
  stats_counter_add(self->metrics.window_available, value);
  return window_size_counter_add(&self->window_size, value, suspended);
}

static inline gsize
_window_size_sub(LogSource *self, gsize value, gboolean *suspended)
{
  stats_counter_sub(self->metrics.window_available, value);
  return window_size_counter_sub(&self->window_size, value, suspended);
}

static inline gsize
_request_dynamic_window(LogSource *self, gsize size)
{
  main_loop_assert_main_thread();

  gsize offered_dynamic = dynamic_window_request(&self->dynamic_window, size);

  self->full_window_size += offered_dynamic;
  stats_counter_add(self->metrics.window_capacity, offered_dynamic);

  return offered_dynamic;
}

static inline void
_release_dynamic_window(LogSource *self, gsize size)
{
  main_loop_assert_main_thread();

  self->full_window_size -= size;
  stats_counter_sub(self->metrics.window_capacity, size);
  dynamic_window_release(&self->dynamic_window, size);
}

static void
_request_dynamic_window_reclamation(LogSource *self, gsize window_size)
{
  g_assert(self->full_window_size - window_size >= self->initial_window_size);
  atomic_gssize_add(&self->window_size_to_be_reclaimed, window_size);
}

guint32
log_source_gather_dynamic_window_reclamation(LogSource *self, guint32 window_size_increment)
{
  guint32 remaining_window_size_increment;
  gssize reclaim;

  gssize to_be_reclaimed, new_to_be_reclaimed;
  do
    {
      to_be_reclaimed = atomic_gssize_get(&self->window_size_to_be_reclaimed);

      if (G_LIKELY(to_be_reclaimed == 0))
        return window_size_increment;

      reclaim = MIN(window_size_increment, to_be_reclaimed);
      remaining_window_size_increment = window_size_increment - reclaim;
      new_to_be_reclaimed = to_be_reclaimed - reclaim;
    }
  while (!atomic_gssize_compare_and_exchange(&self->window_size_to_be_reclaimed, to_be_reclaimed, new_to_be_reclaimed));

  atomic_gssize_add(&self->pending_reclamation, reclaim);
  return remaining_window_size_increment;
}

static void
_release_reclaimed_dynamic_window(LogSource *self)
{
  {
    /* log_source_gather_dynamic_window_reclamation() should only be called when messages are ACKed
     * (in _flow_control_window_size_adjust()), but a race condition in _dec_balanced() has to be compensated here.
     */
    gsize empty_window = window_size_counter_get(&self->window_size, NULL);
    (void) log_source_gather_dynamic_window_reclamation(self, empty_window);
  }

  gssize total_reclaim = atomic_gssize_set_and_get(&self->pending_reclamation, 0);

  if (total_reclaim <= 0)
    return;

  _release_dynamic_window(self, total_reclaim);

  msg_trace("Reclaiming window",
            log_pipe_location_tag(&self->super),
            evt_tag_printf("connection", "%p", self),
            evt_tag_long("total_reclaim", total_reclaim));
}

static inline gsize
_get_dynamic_window_size(LogSource *self)
{
  return self->full_window_size - self->initial_window_size;
}

/* runs in the main thread, the source is idle */
static void
_inc_balanced(LogSource *self, gsize inc)
{
  gsize old_full_window_size = self->full_window_size;

  gsize offered_dynamic = _request_dynamic_window(self, inc);
  gsize old_window_size = _window_size_add(self, offered_dynamic, NULL);
  if (old_window_size == 0 && offered_dynamic != 0)
    log_source_wakeup(self);

  msg_trace("Balance::increase",
            log_pipe_location_tag(&self->super),
            evt_tag_printf("connection", "%p", self),
            evt_tag_int("old_full_window_size", old_full_window_size),
            evt_tag_int("new_full_window_size", self->full_window_size));
}

/* runs in the main thread, the source is idle */
static void
_dec_balanced(LogSource *self, gsize dec)
{
  if (dec == 0)
    return;

  gsize empty_window = window_size_counter_get(&self->window_size, NULL);
  gsize to_decrement_now = dec;
  gsize to_reclaim_later = 0;

  /* '<=' because we can't suspend the source here */
  if (empty_window <= dec)
    {
      /* 1 empty slot has to remain because we can't suspend the source here */
      to_decrement_now = MAX(((gssize) empty_window) - 1, 0);
      to_reclaim_later = dec - to_decrement_now;

      /* there is a race window between window_size_counter_get() and this,
       * which has to be compensated in _release_reclaimed_dynamic_window()
       */
      _request_dynamic_window_reclamation(self, to_reclaim_later);
    }

  gsize new_full_window_size = self->full_window_size - to_decrement_now;
  msg_trace("Balance::decrease",
            log_pipe_location_tag(&self->super),
            evt_tag_printf("connection", "%p", self),
            evt_tag_int("old_full_window_size", self->full_window_size),
            evt_tag_int("new_full_window_size", new_full_window_size),
            evt_tag_int("to_be_reclaimed", to_reclaim_later));

  _window_size_sub(self, to_decrement_now, NULL);
  _release_dynamic_window(self, to_decrement_now);
}

/* runs in the main thread, the source is idle */
static void
_rebalance_dynamic_window(LogSource *self)
{
  gsize current_dynamic_win = _get_dynamic_window_size(self);
  gboolean have_to_increase = current_dynamic_win < self->dynamic_window.pool->balanced_window;
  gboolean have_to_decrease = current_dynamic_win > self->dynamic_window.pool->balanced_window;

  msg_trace("Rebalance dynamic window",
            log_pipe_location_tag(&self->super),
            evt_tag_printf("connection", "%p", self),
            evt_tag_int("full_window", self->full_window_size),
            evt_tag_int("dynamic_win", current_dynamic_win),
            evt_tag_int("static_window", self->initial_window_size),
            evt_tag_int("balanced_window", self->dynamic_window.pool->balanced_window),
            evt_tag_int("avg_free", dynamic_window_stat_get_avg(&self->dynamic_window.stat)));

  if (have_to_increase)
    _inc_balanced(self, self->dynamic_window.pool->balanced_window - current_dynamic_win);
  else if (have_to_decrease)
    _dec_balanced(self, current_dynamic_win - self->dynamic_window.pool->balanced_window);
}

/* runs in the main thread, the source is idle */
void
log_source_dynamic_window_realloc(LogSource *self)
{
  main_loop_assert_main_thread();

  /* it is safe to assume that the window size is not decremented while this function runs,
   * only incrementation is possible by destination threads */

  _release_reclaimed_dynamic_window(self);

  /* must be checked AFTER _release_reclaimed_dynamic_window() */
  if (!atomic_gssize_get(&self->window_size_to_be_reclaimed))
    _rebalance_dynamic_window(self);

  dynamic_window_stat_reset(&self->dynamic_window.stat);
}

void
log_source_release_available_dynamic_window(LogSource *self)
{
  main_loop_assert_main_thread();
  g_assert((self->super.flags & PIF_INITIALIZED) == 0);

  if (!dynamic_window_is_enabled(&self->dynamic_window))
    return;

  gsize empty_window = window_size_counter_get(&self->window_size, NULL);
  gsize dynamic_part = _get_dynamic_window_size(self);

  gsize empty_dynamic = MIN(empty_window, dynamic_part);

  msg_trace("Releasing available dynamic part of the window",
            evt_tag_int("dynamic_window_to_be_released", empty_dynamic),
            log_pipe_location_tag(&self->super));

  _window_size_sub(self, empty_dynamic, NULL);
  _release_dynamic_window(self, empty_dynamic);
}

void
log_source_release_all_dynamic_window(LogSource *self)
{
  g_assert(self->ack_tracker == NULL);
  main_loop_assert_main_thread();

  _release_reclaimed_dynamic_window(self);

  gsize dynamic_part = _get_dynamic_window_size(self);
  msg_trace("Releasing dynamic part of the window", evt_tag_int("dynamic_window_to_be_released", dynamic_part),
            log_pipe_location_tag(&self->super));

  _window_size_sub(self, dynamic_part, NULL);
  _release_dynamic_window(self, dynamic_part);

  dynamic_window_pool_unref(self->dynamic_window.pool);
}

void
log_source_update_dynamic_window_statistics(LogSource *self)
{
  dynamic_window_stat_update(&self->dynamic_window.stat, window_size_counter_get(&self->window_size, NULL));
  msg_trace("Updating dynamic window statistic", evt_tag_int("avg window size",
                                                             dynamic_window_stat_get_avg(&self->dynamic_window.stat)));
}

void
log_source_enable_dynamic_window(LogSource *self, DynamicWindowPool *window_pool)
{
  dynamic_window_set_pool(&self->dynamic_window, dynamic_window_pool_ref(window_pool));
}
