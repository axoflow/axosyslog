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
#include <criterion/criterion.h>
#include "libtest/cr_template.h"

#include "logbalancer.h"
#include "mainloop-worker.h"
#include "apphook.h"

#define NUM_THREADS 4

static LogBalancerOptions options;

static LogBalancer *
_construct_balancer(gint num_targets)
{
  cr_assert(log_balancer_options_init(&options, configuration));
  return log_balancer_new(&options, num_targets);
}

static gint
_select(LogBalancer *balancer)
{
  LogMessage *msg = create_sample_message();
  gint target = log_balancer_select(balancer, msg);
  log_msg_unref(msg);

  cr_assert(target >= 0 && target < log_balancer_get_num_targets(balancer),
            "target out of range: %d", target);
  return target;
}

static void
_assert_selections(LogBalancer *balancer, const gint *expected, gint num_expected)
{
  for (gint i = 0; i < num_expected; i++)
    {
      gint target = _select(balancer);
      cr_assert_eq(target, expected[i], "unexpected target at selection #%d, expected=%d, actual=%d",
                   i, expected[i], target);
    }
}

/* runs the selections in a separate thread with its own thread index */

typedef struct
{
  LogBalancer *balancer;
  gint num_selections;
  gint *targets;
  gint thread_index;

  /* if set, wait until this many threads have started */
  gint *started;
  gint num_threads;
} SelectorThread;

static gpointer
_selector_thread(gpointer user_data)
{
  SelectorThread *st = user_data;

  main_loop_worker_thread_start(MLW_THREADED_INPUT_WORKER);
  st->thread_index = main_loop_worker_get_thread_index();
  if (st->started)
    {
      /* keep our thread index until all the others have allocated theirs */
      g_atomic_int_inc(st->started);
      while (g_atomic_int_get(st->started) < st->num_threads)
        g_thread_yield();
    }
  for (gint i = 0; i < st->num_selections; i++)
    st->targets[i] = _select(st->balancer);
  main_loop_worker_thread_stop();
  return NULL;
}

static void
_run_selector_thread(SelectorThread *st)
{
  GThread *thread = g_thread_new("selector", _selector_thread, st);
  g_thread_join(thread);
}

Test(logbalancer, single_target_always_selects_zero)
{
  LogBalancer *balancer = _construct_balancer(1);

  for (gint i = 0; i < 10; i++)
    cr_assert_eq(_select(balancer), 0);
  log_balancer_free(balancer);
}

Test(logbalancer, round_robin_without_thread_index_uses_shared_sequence)
{
  LogBalancer *balancer = _construct_balancer(3);

  cr_assert_eq(main_loop_worker_get_thread_index(), -1);
  const gint expected[] = { 0, 1, 2, 0, 1, 2, 0 };
  _assert_selections(balancer, expected, G_N_ELEMENTS(expected));
  log_balancer_free(balancer);
}

Test(logbalancer, batched_round_robin_without_thread_index)
{
  options.batch_size = 3;
  LogBalancer *balancer = _construct_balancer(2);

  const gint expected[] = { 0, 0, 0, 1, 1, 1, 0, 0, 0, 1 };
  _assert_selections(balancer, expected, G_N_ELEMENTS(expected));
  log_balancer_free(balancer);
}

Test(logbalancer, round_robin_in_input_thread)
{
  LogBalancer *balancer = _construct_balancer(3);

  gint targets[7];
  SelectorThread st = { .balancer = balancer, .num_selections = G_N_ELEMENTS(targets), .targets = targets };
  _run_selector_thread(&st);

  cr_assert(st.thread_index >= 0);
  for (gint i = 0; i < G_N_ELEMENTS(targets); i++)
    cr_assert_eq(targets[i], (st.thread_index + i) % 3);
  log_balancer_free(balancer);
}

Test(logbalancer, batched_round_robin_in_input_thread)
{
  options.batch_size = 2;
  LogBalancer *balancer = _construct_balancer(3);

  gint targets[8];
  SelectorThread st = { .balancer = balancer, .num_selections = G_N_ELEMENTS(targets), .targets = targets };
  _run_selector_thread(&st);

  cr_assert(st.thread_index >= 0);
  for (gint i = 0; i < G_N_ELEMENTS(targets); i++)
    cr_assert_eq(targets[i], (st.thread_index + i / 2) % 3);
  log_balancer_free(balancer);
}

Test(logbalancer, input_threads_start_on_different_targets)
{
  LogBalancer *balancer = _construct_balancer(NUM_THREADS);

  SelectorThread st[NUM_THREADS];
  gint targets[NUM_THREADS][1];
  GThread *threads[NUM_THREADS];
  gint started = 0;

  /* run them concurrently, so that each of them has a different thread index */
  for (gint i = 0; i < NUM_THREADS; i++)
    {
      st[i] = (SelectorThread)
      {
        .balancer = balancer, .num_selections = 1, .targets = targets[i],
        .started = &started, .num_threads = NUM_THREADS,
      };
      threads[i] = g_thread_new("selector", _selector_thread, &st[i]);
    }
  for (gint i = 0; i < NUM_THREADS; i++)
    g_thread_join(threads[i]);

  gboolean seen[NUM_THREADS] = { 0 };
  for (gint i = 0; i < NUM_THREADS; i++)
    {
      cr_assert_eq(targets[i][0], st[i].thread_index % NUM_THREADS);
      seen[targets[i][0]] = TRUE;
    }
  for (gint i = 0; i < NUM_THREADS; i++)
    cr_assert(seen[i], "target %d was not selected by any of the threads", i);

  log_balancer_free(balancer);
}

static void
setup(void)
{
  app_startup();

  configuration = cfg_new_snippet();
  cr_assert(cfg_init(configuration));

  main_loop_worker_allocate_thread_space(NUM_THREADS);
  main_loop_worker_finalize_thread_space();
  log_balancer_options_defaults(&options);
}

static void
teardown(void)
{
  log_balancer_options_destroy(&options);
  cfg_free(configuration);
  app_shutdown();
}

TestSuite(logbalancer, .init = setup, .fini = teardown);
