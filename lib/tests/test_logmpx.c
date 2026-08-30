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

#include "logmpx.h"
#include "apphook.h"

/* a mock destination pipe standing in for a real driver: when "down", it
 * behaves the way flags(destination-failover) expects a destination to
 * behave once it decides it can't currently deliver -- reject via *matched
 * and drop, instead of accepting the message. A real driver only does this
 * when its own destination-failover() option is explicitly enabled (see
 * lib/driver.h LogDestDriver.destination_failover) -- ordinary destinations
 * never reject like this, they keep buffering. This test exercises
 * LogMultiplexer's branch-selection loop directly, standing in for that
 * driver-level decision.
 *
 * NOTE: this does NOT exercise the real compiled pipe graph, where every
 * destination reference is wrapped in its own "mpx(destination-reference)"
 * multiplexer (cfg_tree_compile_reference(), ENC_DESTINATION case) and a
 * named destination's own body is wrapped in a second
 * "mpx(destination-junction)" (cfg_tree_compile_junction(), ENC_DESTINATION
 * case) -- both of those must also keep delivery propagation enabled for a
 * destination-failover branch, or the destination's rejection never reaches
 * here at all. log_multiplexer_enable_delivery_propagation_downstream(),
 * which is what makes that happen, is exercised directly further below;
 * verifying it against an actual compiled configuration is left to the
 * light/functional tests. */
typedef struct _FailoverMockPipe
{
  LogPipe super;
  GPtrArray *captured_messages;
  gboolean down;
} FailoverMockPipe;

static void
_queue(LogPipe *s, LogMessage *msg, const LogPathOptions *path_options)
{
  FailoverMockPipe *self = (FailoverMockPipe *) s;

  if (self->down)
    {
      if (path_options->matched)
        *path_options->matched = FALSE;
      log_msg_drop(msg, path_options, AT_PROCESSED);
      return;
    }

  g_ptr_array_add(self->captured_messages, log_msg_ref(msg));
  log_pipe_forward_msg(s, msg, path_options);
}

static void
_free(LogPipe *s)
{
  FailoverMockPipe *self = (FailoverMockPipe *) s;

  g_ptr_array_free(self->captured_messages, TRUE);
  log_pipe_free_method(s);
}

static FailoverMockPipe *
_failover_mock_pipe_new(GlobalConfig *cfg, gboolean down)
{
  FailoverMockPipe *self = g_new0(FailoverMockPipe, 1);

  log_pipe_init_instance(&self->super, cfg);
  self->super.queue = _queue;
  self->super.free_fn = _free;
  self->captured_messages = g_ptr_array_new_full(0, (GDestroyNotify) log_msg_unref);
  self->down = down;
  return self;
}

/* mirrors what cfg_tree_propagate_expr_node_properties_to_pipe() does for
 * flags(destination-failover): it maps onto PIF_BRANCH_FINAL, so the
 * multiplexer stops trying further branches as soon as one accepts. */
static LogMultiplexer *
_create_destination_failover_mpx(GlobalConfig *cfg, FailoverMockPipe **branches, gint n)
{
  LogMultiplexer *mpx = log_multiplexer_new(cfg);

  for (gint i = 0; i < n; i++)
    {
      branches[i]->super.flags |= PIF_BRANCH_FINAL;
      log_pipe_init(&branches[i]->super);
      log_multiplexer_add_next_hop(mpx, &branches[i]->super);
    }
  return mpx;
}

static void
_queue_empty_message(LogPipe *pipe)
{
  LogPathOptions path_options = LOG_PATH_OPTIONS_INIT_NOACK;
  LogMessage *msg = log_msg_new_empty();

  log_pipe_queue(pipe, msg, &path_options);
}

static void
_free_branches(FailoverMockPipe **branches, gint n)
{
  for (gint i = 0; i < n; i++)
    {
      log_pipe_deinit(&branches[i]->super);
      log_pipe_unref(&branches[i]->super);
    }
}

Test(logmpx, destination_failover_routes_to_first_reachable_branch)
{
  FailoverMockPipe *branches[3];

  branches[0] = _failover_mock_pipe_new(NULL, TRUE);
  branches[1] = _failover_mock_pipe_new(NULL, FALSE);
  branches[2] = _failover_mock_pipe_new(NULL, FALSE);

  LogMultiplexer *mpx = _create_destination_failover_mpx(NULL, branches, 3);
  cr_assert(log_pipe_init(&mpx->super));

  _queue_empty_message(&mpx->super);

  cr_assert_eq(branches[0]->captured_messages->len, 0, "the down primary must not accept the message");
  cr_assert_eq(branches[1]->captured_messages->len, 1, "the first branch that accepts gets the message");
  cr_assert_eq(branches[2]->captured_messages->len, 0,
               "later branches are not tried once an earlier one accepted (implicit final)");

  log_pipe_deinit(&mpx->super);
  log_pipe_unref(&mpx->super);
  _free_branches(branches, 3);
}

Test(logmpx, destination_failover_message_is_lost_when_all_branches_are_down)
{
  FailoverMockPipe *branches[2];

  branches[0] = _failover_mock_pipe_new(NULL, TRUE);
  branches[1] = _failover_mock_pipe_new(NULL, TRUE);

  LogMultiplexer *mpx = _create_destination_failover_mpx(NULL, branches, 2);
  cr_assert(log_pipe_init(&mpx->super));

  _queue_empty_message(&mpx->super);

  cr_assert_eq(branches[0]->captured_messages->len, 0);
  cr_assert_eq(branches[1]->captured_messages->len, 0,
               "with every branch down, the message is lost -- nothing forces it into the last one");

  log_pipe_deinit(&mpx->super);
  log_pipe_unref(&mpx->super);
  _free_branches(branches, 2);
}

Test(logmpx, destination_failover_switches_when_reachability_changes)
{
  FailoverMockPipe *branches[2];

  branches[0] = _failover_mock_pipe_new(NULL, FALSE);
  branches[1] = _failover_mock_pipe_new(NULL, TRUE);

  LogMultiplexer *mpx = _create_destination_failover_mpx(NULL, branches, 2);
  cr_assert(log_pipe_init(&mpx->super));

  _queue_empty_message(&mpx->super);
  cr_assert_eq(branches[0]->captured_messages->len, 1);
  cr_assert_eq(branches[1]->captured_messages->len, 0);

  /* primary goes down, secondary comes up: new messages must follow */
  branches[0]->down = TRUE;
  branches[1]->down = FALSE;

  _queue_empty_message(&mpx->super);
  cr_assert_eq(branches[0]->captured_messages->len, 1, "no more messages routed to the now-down primary");
  cr_assert_eq(branches[1]->captured_messages->len, 1, "new messages follow to the now-reachable secondary");

  /* and immediately back once the primary recovers */
  branches[0]->down = FALSE;
  branches[1]->down = TRUE;

  _queue_empty_message(&mpx->super);
  cr_assert_eq(branches[0]->captured_messages->len, 2, "failback happens as soon as the primary is reachable");

  log_pipe_deinit(&mpx->super);
  log_pipe_unref(&mpx->super);
  _free_branches(branches, 2);
}

Test(logmpx, enable_delivery_propagation_downstream_crosses_pipe_next)
{
  /* simulates a destination with some processing between two nested
   * multiplexers, e.g. destination NAME { channel { rewrite(x);
   * destination(other); }; }; -- "other"'s own reference wrapper and
   * junction are reachable only via pipe_next from the rewrite pipe, not
   * via next_hops:
   *
   *   mpx1 --(next_hop)--> plain_pipe --(pipe_next)--> mpx2 --(next_hop)--> mpx3
   */
  LogMultiplexer *mpx1 = log_multiplexer_new(NULL);
  LogPipe *plain_pipe = log_pipe_new(NULL);
  LogMultiplexer *mpx2 = log_multiplexer_new(NULL);
  LogMultiplexer *mpx3 = log_multiplexer_new(NULL);

  log_multiplexer_disable_delivery_propagation(mpx1);
  log_multiplexer_disable_delivery_propagation(mpx2);
  log_multiplexer_disable_delivery_propagation(mpx3);

  log_multiplexer_add_next_hop(mpx1, plain_pipe);
  log_pipe_append(plain_pipe, &mpx2->super);
  log_multiplexer_add_next_hop(mpx2, &mpx3->super);

  log_multiplexer_enable_delivery_propagation_downstream(&mpx1->super);

  cr_assert(mpx1->delivery_propagation, "the starting pipe itself must be enabled");
  cr_assert(mpx2->delivery_propagation,
            "reached via next_hop -> pipe_next -- next_hops alone would miss this");
  cr_assert(mpx3->delivery_propagation, "reached transitively via mpx2's own next_hop");

  log_pipe_unref(&mpx1->super);
  log_pipe_unref(plain_pipe);
  log_pipe_unref(&mpx2->super);
  log_pipe_unref(&mpx3->super);
}

static void
setup(void)
{
  app_startup();
}

static void
teardown(void)
{
  app_shutdown();
}

TestSuite(logmpx, .init = setup, .fini = teardown);
