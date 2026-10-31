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
#include "logbalancer-autoscale.h"
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
_select_msg(LogBalancer *balancer, LogMessage *msg)
{
  gint target = log_balancer_select(balancer, msg, FALSE);

  cr_assert(target >= 0 && target < log_balancer_get_num_targets(balancer),
            "target out of range: %d", target);
  return target;
}

static gint
_select(LogBalancer *balancer)
{
  LogMessage *msg = create_sample_message();
  gint target = _select_msg(balancer, msg);
  log_msg_unref(msg);
  return target;
}

static LogMessage *
_create_keyed_message(const gchar *key, gint64 recvd_usec)
{
  LogMessage *msg = create_empty_message();

  log_msg_set_value_by_name(msg, "KEY", key, -1);
  msg->timestamps[LM_TS_RECVD].ut_usec = recvd_usec;
  return msg;
}

static gint
_select_key(LogBalancer *balancer, const gchar *key, gint64 recvd_usec)
{
  LogMessage *msg = _create_keyed_message(key, recvd_usec);
  gint target = _select_msg(balancer, msg);
  log_msg_unref(msg);
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

/* sticky */

Test(logbalancer, sticky_selection_is_based_on_the_hash_of_the_key)
{
  log_balancer_options_set_partition_key_ref(&options, compile_template("$KEY"));
  LogBalancer *balancer = _construct_balancer(7);

  const gchar *keys[] = { "foo", "bar", "baz", "qux", "quux", "corge", "grault" };
  for (gint i = 0; i < G_N_ELEMENTS(keys); i++)
    {
      gint expected = g_str_hash(keys[i]) % 7;

      /* repeated selections of the same key yield the same target */
      for (gint j = 0; j < 5; j++)
        cr_assert_eq(_select_key(balancer, keys[i], j * 100000), expected,
                     "key %s was not mapped to the expected target %d", keys[i], expected);
    }
  log_balancer_free(balancer);
}

Test(logbalancer, sticky_selection_ignores_batch_size)
{
  options.batch_size = 10;
  log_balancer_options_set_partition_key_ref(&options, compile_template("$KEY"));
  LogBalancer *balancer = _construct_balancer(5);

  for (gint i = 0; i < 20; i++)
    {
      gchar key[16];
      g_snprintf(key, sizeof(key), "key%d", i);
      cr_assert_eq(_select_key(balancer, key, 0), g_str_hash(key) % 5);
    }
  log_balancer_free(balancer);
}

Test(logbalancer, sticky_selection_with_buckets_spreads_a_key_over_consecutive_targets)
{
  log_balancer_options_set_partition_key_ref(&options, compile_template("$KEY"));
  log_balancer_options_set_partition_buckets_ref(&options, compile_template("3"));
  LogBalancer *balancer = _construct_balancer(8);

  guint hash = g_str_hash("foo");
  gboolean seen[8] = { 0 };

  /* the second is cut to 3 buckets based on the receipt time */
  for (gint64 usec = 0; usec < 1000000; usec += 50000)
    {
      gint target = _select_key(balancer, "foo", usec);
      gint bucket = usec * 3 / 1000000;

      cr_assert_eq(target, (hash + bucket) % 8, "unexpected target at usec=%" G_GINT64_FORMAT, usec);
      seen[target] = TRUE;
    }

  gint num_seen = 0;
  for (gint i = 0; i < 8; i++)
    num_seen += seen[i];
  cr_assert_eq(num_seen, 3);
  log_balancer_free(balancer);
}

Test(logbalancer, sticky_selection_with_templated_buckets)
{
  log_balancer_options_set_partition_key_ref(&options, compile_template("$KEY"));
  log_balancer_options_set_partition_buckets_ref(&options, compile_template("${BUCKETS}"));
  LogBalancer *balancer = _construct_balancer(8);

  guint hash = g_str_hash("foo");

  LogMessage *msg = _create_keyed_message("foo", 500000);
  log_msg_set_value_by_name(msg, "BUCKETS", "2", -1);
  cr_assert_eq(_select_msg(balancer, msg), (hash + 1) % 8);
  log_msg_unref(msg);

  /* invalid buckets value disables bucketing */
  msg = _create_keyed_message("foo", 500000);
  log_msg_set_value_by_name(msg, "BUCKETS", "invalid", -1);
  cr_assert_eq(_select_msg(balancer, msg), hash % 8);
  log_msg_unref(msg);

  log_balancer_free(balancer);
}

Test(logbalancer, sticky_selection_with_literal_partition_key_is_rejected)
{
  log_balancer_options_set_partition_key_ref(&options, compile_template("literal"));
  cr_assert_not(log_balancer_options_init(&options, configuration));
}

/* availability */

static gboolean
_is_available(gint target, gpointer user_data)
{
  guint *available_mask = user_data;

  return (*available_mask & (1 << target)) != 0;
}

static gint
_select_available(LogBalancer *balancer, const gchar *key)
{
  LogMessage *msg = key ? _create_keyed_message(key, 0) : create_sample_message();
  gint target = log_balancer_select(balancer, msg, TRUE);
  log_msg_unref(msg);
  return target;
}

Test(logbalancer, round_robin_skips_unavailable_targets)
{
  LogBalancer *balancer = _construct_balancer(4);
  guint available_mask = 0x0f & ~(1 << 1) & ~(1 << 2);
  log_balancer_set_availability_check(balancer, _is_available, &available_mask);

  /* without a thread index */
  for (gint i = 0; i < 12; i++)
    {
      gint target = _select_available(balancer, NULL);
      cr_assert(target == 0 || target == 3, "unavailable target selected: %d", target);
    }

  /* not requesting available targets only ignores the availability check */
  const gint expected[] = { 0, 1, 2, 3 };
  _assert_selections(balancer, expected, G_N_ELEMENTS(expected));
  log_balancer_free(balancer);
}

typedef struct
{
  LogBalancer *balancer;
  gint targets[12];
} AvailableSelectorThread;

static gpointer
_available_selector_thread(gpointer user_data)
{
  AvailableSelectorThread *st = user_data;

  main_loop_worker_thread_start(MLW_THREADED_INPUT_WORKER);
  for (gint i = 0; i < G_N_ELEMENTS(st->targets); i++)
    st->targets[i] = _select_available(st->balancer, NULL);
  main_loop_worker_thread_stop();
  return NULL;
}

Test(logbalancer, round_robin_in_input_thread_skips_unavailable_targets)
{
  options.batch_size = 2;
  LogBalancer *balancer = _construct_balancer(4);
  guint available_mask = 0x0f & ~(1 << 1) & ~(1 << 2);
  log_balancer_set_availability_check(balancer, _is_available, &available_mask);

  AvailableSelectorThread st = { .balancer = balancer };
  g_thread_join(g_thread_new("selector", _available_selector_thread, &st));

  /* runs of 2 on target 0 and 3 */
  for (gint i = 0; i < G_N_ELEMENTS(st.targets); i++)
    {
      cr_assert(st.targets[i] == 0 || st.targets[i] == 3, "unavailable target selected: %d", st.targets[i]);
      if (i % 2 == 1)
        cr_assert_eq(st.targets[i], st.targets[i - 1]);
      else if (i > 0)
        cr_assert_neq(st.targets[i], st.targets[i - 1]);
    }
  log_balancer_free(balancer);
}

Test(logbalancer, round_robin_returns_minus_one_if_no_target_is_available)
{
  LogBalancer *balancer = _construct_balancer(3);
  guint available_mask = 0;
  log_balancer_set_availability_check(balancer, _is_available, &available_mask);

  cr_assert_eq(_select_available(balancer, NULL), -1);

  AvailableSelectorThread st = { .balancer = balancer };
  g_thread_join(g_thread_new("selector", _available_selector_thread, &st));
  for (gint i = 0; i < G_N_ELEMENTS(st.targets); i++)
    cr_assert_eq(st.targets[i], -1);

  /* targets becoming available again are selected */
  available_mask = 1 << 2;
  cr_assert_eq(_select_available(balancer, NULL), 2);
  log_balancer_free(balancer);
}

Test(logbalancer, single_target_availability)
{
  LogBalancer *balancer = _construct_balancer(1);
  guint available_mask = 0;
  log_balancer_set_availability_check(balancer, _is_available, &available_mask);

  cr_assert_eq(_select_available(balancer, NULL), -1);
  available_mask = 1;
  cr_assert_eq(_select_available(balancer, NULL), 0);
  log_balancer_free(balancer);
}

Test(logbalancer, sticky_selection_does_not_move_keys_of_unavailable_targets)
{
  log_balancer_options_set_partition_key_ref(&options, compile_template("$KEY"));
  LogBalancer *balancer = _construct_balancer(4);
  gint target = g_str_hash("foo") % 4;
  guint available_mask = 0x0f & ~(1 << target);
  log_balancer_set_availability_check(balancer, _is_available, &available_mask);

  cr_assert_eq(_select_available(balancer, "foo"), -1);

  available_mask = 0x0f;
  cr_assert_eq(_select_available(balancer, "foo"), target);
  log_balancer_free(balancer);
}

/* autoscaling */

static gint
_autoscale_select(LogBalancerAutoscale *autoscale, const gchar *key, gint64 msec)
{
  struct timespec now = { .tv_sec = 1000 + msec / 1000, .tv_nsec = (msec % 1000) * 1000000 };
  LogMessage *msg = _create_keyed_message(key, (msec % 1000) * 1000);

  gint target = log_balancer_autoscale_select_at(autoscale, key, msg, &now);
  log_msg_unref(msg);
  return target;
}

/* the number of distinct targets of a key, sampled within a single second */
static gint
_autoscale_count_targets(LogBalancerAutoscale *autoscale, const gchar *key, gint64 msec, gint num_targets)
{
  gboolean seen[16] = { 0 };
  gint count = 0;

  for (gint i = 0; i < 100; i++)
    {
      gint target = _autoscale_select(autoscale, key, msec + i * 10);

      cr_assert(target >= 0 && target < num_targets, "target out of range: %d", target);
      if (!seen[target])
        count++;
      seen[target] = TRUE;
    }
  return count;
}

Test(logbalancer, autoscale_single_partition_uses_all_but_wfo_targets)
{
  LogBalancerAutoscale *autoscale = log_balancer_autoscale_new(4, 1);

  cr_assert_eq(_autoscale_count_targets(autoscale, "foo", 0, 4), 3);
  cr_assert_eq(log_balancer_autoscale_get_num_partitions(autoscale), 1);
  log_balancer_autoscale_free(autoscale);
}

Test(logbalancer, autoscale_assigns_more_targets_to_busy_partitions)
{
  LogBalancerAutoscale *autoscale = log_balancer_autoscale_new(8, 1);

  /* "busy" receives a message every 10ms, "quiet" one every second */
  gint64 msec;
  for (msec = 0; msec < 20000; msec += 10)
    {
      _autoscale_select(autoscale, "busy", msec);
      if (msec % 1000 == 0)
        _autoscale_select(autoscale, "quiet", msec);
    }

  gint busy_targets = _autoscale_count_targets(autoscale, "busy", msec, 8);
  gint quiet_targets = _autoscale_count_targets(autoscale, "quiet", msec, 8);

  /* "busy" gets the bulk of the 7 non-WFO targets, "quiet" is an orphan
   * sharing the remaining ones */
  cr_assert_gt(busy_targets, quiet_targets, "busy=%d, quiet=%d", busy_targets, quiet_targets);
  cr_assert_geq(busy_targets, 5, "busy=%d", busy_targets);
  log_balancer_autoscale_free(autoscale);
}

Test(logbalancer, autoscale_expires_inactive_partitions)
{
  LogBalancerAutoscale *autoscale = log_balancer_autoscale_new(4, 1);
  StatsCounterItem active_partitions = { 0 };
  log_balancer_autoscale_set_active_partitions_counter(autoscale, &active_partitions);

  _autoscale_select(autoscale, "foo", 0);
  _autoscale_select(autoscale, "bar", 0);
  cr_assert_eq(log_balancer_autoscale_get_num_partitions(autoscale), 2);
  cr_assert_eq(stats_counter_get(&active_partitions), 2);

  /* "foo" stays active, "bar" expires after 10 half-lives (300s) */
  gint64 msec;
  for (msec = 0; msec <= 310000; msec += 5000)
    _autoscale_select(autoscale, "foo", msec);

  cr_assert_eq(log_balancer_autoscale_get_num_partitions(autoscale), 1);
  cr_assert_eq(stats_counter_get(&active_partitions), 1);
  log_balancer_autoscale_free(autoscale);
}

Test(logbalancer, sticky_selection_with_autoscaling)
{
  log_balancer_options_set_partition_key_ref(&options, compile_template("$KEY"));
  options.partition_autoscaling = TRUE;
  LogBalancer *balancer = _construct_balancer(4);

  StatsCounterItem active_partitions = { 0 };
  log_balancer_set_active_partitions_counter(balancer, &active_partitions);

  /* a single partition gets all the targets, but the one reserved for orphans */
  gboolean seen[4] = { 0 };
  for (gint64 usec = 0; usec < 1000000; usec += 10000)
    seen[_select_key(balancer, "foo", usec)] = TRUE;

  cr_assert(seen[0] && seen[1] && seen[2]);
  cr_assert_not(seen[3]);
  cr_assert_eq(stats_counter_get(&active_partitions), 1);
  log_balancer_free(balancer);
}

Test(logbalancer, autoscaling_with_buckets_is_rejected)
{
  log_balancer_options_set_partition_key_ref(&options, compile_template("$KEY"));
  log_balancer_options_set_partition_buckets_ref(&options, compile_template("3"));
  options.partition_autoscaling = TRUE;
  cr_assert_not(log_balancer_options_init(&options, configuration));
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
