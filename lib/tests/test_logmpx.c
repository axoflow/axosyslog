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
 * behaves the way a real destination (log_threaded_dest_driver_queue(),
 * afsocket_dd_reject_if_unreachable()) behaves once it considers itself
 * unreachable while handling a message that arrived via a
 * flags(destination-failover) branch -- reject via *matched and drop,
 * instead of accepting the message. It only does this when
 * path_options->destination_failover is set, exactly mirroring how a real
 * driver never rejects like this for ordinary (non-failover) use of the
 * same destination. This test exercises LogMultiplexer's branch-selection
 * loop directly, standing in for that driver-level decision.
 *
 * NOTE: this does NOT exercise the real compiled pipe graph, where every
 * destination reference is wrapped in its own "mpx(destination-reference)"
 * multiplexer (cfg_tree_compile_reference(), ENC_DESTINATION case) and a
 * named destination's own body is wrapped in a second
 * "mpx(destination-junction)" (cfg_tree_compile_junction(), ENC_DESTINATION
 * case). Both of those disable delivery propagation unconditionally, same
 * as for any ordinary destination -- what makes a destination's rejection
 * reach back through them anyway for a destination-failover branch is
 * path_options->destination_failover itself (see LogMultiplexer.queue()),
 * exercised directly further below; verifying it against an actual
 * compiled configuration is left to the light/functional tests. */
typedef struct _FailoverMockPipe
{
  LogPipe super;
  GPtrArray *captured_messages;
  gboolean down;
  gboolean last_seen_destination_failover;
} FailoverMockPipe;

static void
_queue(LogPipe *s, LogMessage *msg, const LogPathOptions *path_options)
{
  FailoverMockPipe *self = (FailoverMockPipe *) s;

  self->last_seen_destination_failover = path_options->destination_failover;

  if (self->down && path_options->destination_failover)
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

/* mirrors what cfg-tree does for flags(destination-failover): the
 * alternatives are final branches of a multiplexer marked with
 * PIF_DESTINATION_FAILOVER */
static LogMultiplexer *
_create_destination_failover_mpx(GlobalConfig *cfg, LogPipe **branches, gint n)
{
  LogMultiplexer *mpx = log_multiplexer_new(cfg);

  mpx->super.flags |= PIF_DESTINATION_FAILOVER;
  for (gint i = 0; i < n; i++)
    {
      branches[i]->flags |= PIF_BRANCH_FINAL;
      log_pipe_init(branches[i]);
      log_multiplexer_add_next_hop(mpx, branches[i]);
    }
  cr_assert(log_pipe_init(&mpx->super));
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
_free_pipe(LogPipe *pipe)
{
  log_pipe_deinit(pipe);
  log_pipe_unref(pipe);
}

Test(logmpx, destination_failover_routes_to_first_reachable_branch)
{
  FailoverMockPipe *branches[3] =
  {
    _failover_mock_pipe_new(NULL, TRUE),
    _failover_mock_pipe_new(NULL, FALSE),
    _failover_mock_pipe_new(NULL, FALSE),
  };
  LogPipe *pipes[3] = { &branches[0]->super, &branches[1]->super, &branches[2]->super };
  LogMultiplexer *mpx = _create_destination_failover_mpx(NULL, pipes, 3);

  _queue_empty_message(&mpx->super);

  cr_assert_eq(branches[0]->captured_messages->len, 0, "the down primary must not accept the message");
  cr_assert_eq(branches[1]->captured_messages->len, 1, "the first branch that accepts gets the message");
  cr_assert_eq(branches[2]->captured_messages->len, 0, "later branches are not tried once an earlier one accepted");

  _free_pipe(&mpx->super);
  for (gint i = 0; i < 3; i++)
    _free_pipe(pipes[i]);
}

Test(logmpx, destination_failover_last_branch_buffers_when_every_other_is_down)
{
  FailoverMockPipe *branches[2] =
  {
    _failover_mock_pipe_new(NULL, TRUE),
    _failover_mock_pipe_new(NULL, TRUE),
  };
  LogPipe *pipes[2] = { &branches[0]->super, &branches[1]->super };
  LogMultiplexer *mpx = _create_destination_failover_mpx(NULL, pipes, 2);

  _queue_empty_message(&mpx->super);

  cr_assert_eq(branches[0]->captured_messages->len, 0);
  cr_assert(branches[0]->last_seen_destination_failover, "a branch followed by another one may reject");
  cr_assert_not(branches[1]->last_seen_destination_failover, "the last branch has nowhere to fail over to");
  cr_assert_eq(branches[1]->captured_messages->len, 1, "so it accepts (and buffers) the message instead");

  _free_pipe(&mpx->super);
  for (gint i = 0; i < 2; i++)
    _free_pipe(pipes[i]);
}

Test(logmpx, destination_failover_switches_when_reachability_changes)
{
  FailoverMockPipe *branches[2] =
  {
    _failover_mock_pipe_new(NULL, FALSE),
    _failover_mock_pipe_new(NULL, FALSE),
  };
  LogPipe *pipes[2] = { &branches[0]->super, &branches[1]->super };
  LogMultiplexer *mpx = _create_destination_failover_mpx(NULL, pipes, 2);

  _queue_empty_message(&mpx->super);
  cr_assert_eq(branches[0]->captured_messages->len, 1);
  cr_assert_eq(branches[1]->captured_messages->len, 0);

  branches[0]->down = TRUE;
  _queue_empty_message(&mpx->super);
  cr_assert_eq(branches[0]->captured_messages->len, 1, "no more messages routed to the now-down primary");
  cr_assert_eq(branches[1]->captured_messages->len, 1, "new messages follow to the secondary");

  branches[0]->down = FALSE;
  _queue_empty_message(&mpx->super);
  cr_assert_eq(branches[0]->captured_messages->len, 2, "failback happens as soon as the primary is reachable");
  cr_assert_eq(branches[1]->captured_messages->len, 1);

  _free_pipe(&mpx->super);
  for (gint i = 0; i < 2; i++)
    _free_pipe(pipes[i]);
}

Test(logmpx, destination_failover_last_branch_is_a_fallback_branch_if_there_is_one)
{
  FailoverMockPipe *fallback = _failover_mock_pipe_new(NULL, TRUE);
  FailoverMockPipe *primary = _failover_mock_pipe_new(NULL, TRUE);
  FailoverMockPipe *secondary = _failover_mock_pipe_new(NULL, TRUE);

  fallback->super.flags |= PIF_BRANCH_FALLBACK;
  LogPipe *pipes[3] = { &fallback->super, &primary->super, &secondary->super };
  LogMultiplexer *mpx = _create_destination_failover_mpx(NULL, pipes, 3);

  _queue_empty_message(&mpx->super);

  cr_assert(secondary->last_seen_destination_failover, "the fallback branch is tried after the secondary");
  cr_assert_eq(secondary->captured_messages->len, 0);
  cr_assert_not(fallback->last_seen_destination_failover);
  cr_assert_eq(fallback->captured_messages->len, 1);

  _free_pipe(&mpx->super);
  for (gint i = 0; i < 3; i++)
    _free_pipe(pipes[i]);
}

Test(logmpx, destination_failover_is_not_set_by_an_unmarked_multiplexer)
{
  FailoverMockPipe *first = _failover_mock_pipe_new(NULL, FALSE);
  FailoverMockPipe *second = _failover_mock_pipe_new(NULL, FALSE);
  LogMultiplexer *mpx = log_multiplexer_new(NULL);

  log_pipe_init(&first->super);
  log_pipe_init(&second->super);
  log_multiplexer_add_next_hop(mpx, &first->super);
  log_multiplexer_add_next_hop(mpx, &second->super);
  cr_assert(log_pipe_init(&mpx->super));

  _queue_empty_message(&mpx->super);

  cr_assert_not(first->last_seen_destination_failover);
  cr_assert_not(second->last_seen_destination_failover);

  _free_pipe(&mpx->super);
  _free_pipe(&first->super);
  _free_pipe(&second->super);
}

Test(logmpx, destination_failover_last_branch_of_a_nested_failover_may_reject_if_the_outer_one_has_alternatives)
{
  /* outer: [inner: [a, b], c] -- b is last in inner, but outer can still
   * try c, so b must keep rejecting */
  FailoverMockPipe *a = _failover_mock_pipe_new(NULL, TRUE);
  FailoverMockPipe *b = _failover_mock_pipe_new(NULL, TRUE);
  FailoverMockPipe *c = _failover_mock_pipe_new(NULL, FALSE);
  LogPipe *inner_pipes[2] = { &a->super, &b->super };
  LogMultiplexer *inner = _create_destination_failover_mpx(NULL, inner_pipes, 2);
  LogPipe *outer_pipes[2] = { &inner->super, &c->super };
  LogMultiplexer *outer = _create_destination_failover_mpx(NULL, outer_pipes, 2);

  _queue_empty_message(&outer->super);

  cr_assert(b->last_seen_destination_failover);
  cr_assert_eq(b->captured_messages->len, 0);
  cr_assert_eq(c->captured_messages->len, 1);

  _free_pipe(&outer->super);
  _free_pipe(&inner->super);
  _free_pipe(&a->super);
  _free_pipe(&b->super);
  _free_pipe(&c->super);
}

Test(logmpx, destination_failover_last_branch_of_a_nested_failover_buffers_in_the_outer_last_branch)
{
  /* outer: [a, inner: [b, c]] -- c is last in both */
  FailoverMockPipe *a = _failover_mock_pipe_new(NULL, TRUE);
  FailoverMockPipe *b = _failover_mock_pipe_new(NULL, TRUE);
  FailoverMockPipe *c = _failover_mock_pipe_new(NULL, TRUE);
  LogPipe *inner_pipes[2] = { &b->super, &c->super };
  LogMultiplexer *inner = _create_destination_failover_mpx(NULL, inner_pipes, 2);
  LogPipe *outer_pipes[2] = { &a->super, &inner->super };
  LogMultiplexer *outer = _create_destination_failover_mpx(NULL, outer_pipes, 2);

  _queue_empty_message(&outer->super);

  cr_assert(b->last_seen_destination_failover);
  cr_assert_not(c->last_seen_destination_failover);
  cr_assert_eq(c->captured_messages->len, 1);

  _free_pipe(&outer->super);
  _free_pipe(&inner->super);
  _free_pipe(&a->super);
  _free_pipe(&b->super);
  _free_pipe(&c->super);
}

Test(logmpx, destination_failover_rejection_propagates_through_a_delivery_propagation_disabled_multiplexer)
{
  /* mirrors the real compiled shape for a destination reference: the
   * failover multiplexer -> mpx_ref ("mpx(destination-reference)", which
   * disables delivery propagation) -> leaf (the actual driver, down).  The
   * rejection must still reach the failover multiplexer, so that it tries
   * the next alternative. */
  FailoverMockPipe *leaf = _failover_mock_pipe_new(NULL, TRUE);
  FailoverMockPipe *secondary = _failover_mock_pipe_new(NULL, FALSE);
  LogMultiplexer *mpx_ref = log_multiplexer_new(NULL);

  log_multiplexer_disable_delivery_propagation(mpx_ref);
  log_pipe_init(&leaf->super);
  log_multiplexer_add_next_hop(mpx_ref, &leaf->super);

  LogPipe *pipes[2] = { &mpx_ref->super, &secondary->super };
  LogMultiplexer *mpx = _create_destination_failover_mpx(NULL, pipes, 2);

  _queue_empty_message(&mpx->super);

  cr_assert(leaf->last_seen_destination_failover, "the flag must reach the driver through mpx_ref");
  cr_assert_eq(leaf->captured_messages->len, 0);
  cr_assert_eq(secondary->captured_messages->len, 1,
               "the rejection must reach the failover multiplexer despite mpx_ref disabling delivery propagation");

  _free_pipe(&mpx->super);
  _free_pipe(&mpx_ref->super);
  _free_pipe(&leaf->super);
  _free_pipe(&secondary->super);
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
