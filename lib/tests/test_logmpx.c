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

/* mirrors what cfg_tree_propagate_expr_node_properties_to_pipe() does for
 * flags(destination-failover): PIF_BRANCH_FINAL makes the multiplexer stop
 * trying further branches as soon as one accepts, and
 * PIF_BRANCH_DESTINATION_FAILOVER makes it set
 * LogPathOptions.destination_failover on messages it queues to that
 * branch. */
static LogMultiplexer *
_create_destination_failover_mpx(GlobalConfig *cfg, FailoverMockPipe **branches, gint n)
{
  LogMultiplexer *mpx = log_multiplexer_new(cfg);

  for (gint i = 0; i < n; i++)
    {
      branches[i]->super.flags |= PIF_BRANCH_FINAL | PIF_BRANCH_DESTINATION_FAILOVER;
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

Test(logmpx, destination_failover_path_option_is_set_only_for_flagged_branches)
{
  /* two siblings in the *same* multiplexer, only one flagged
   * destination-failover -- proves the per-next_hop dispatch in
   * log_multiplexer_queue() does not leak the flag from one next_hop onto
   * an unrelated sibling that happens to be dispatched right after it. */
  FailoverMockPipe *flagged = _failover_mock_pipe_new(NULL, FALSE);
  FailoverMockPipe *plain = _failover_mock_pipe_new(NULL, FALSE);

  LogMultiplexer *mpx = log_multiplexer_new(NULL);
  flagged->super.flags |= PIF_BRANCH_DESTINATION_FAILOVER;
  log_pipe_init(&flagged->super);
  log_pipe_init(&plain->super);
  log_multiplexer_add_next_hop(mpx, &flagged->super);
  log_multiplexer_add_next_hop(mpx, &plain->super);
  cr_assert(log_pipe_init(&mpx->super));

  _queue_empty_message(&mpx->super);

  cr_assert(flagged->last_seen_destination_failover, "the flagged branch must see destination_failover=TRUE");
  cr_assert_not(plain->last_seen_destination_failover,
                "an unflagged sibling dispatched right after it must not see it too");

  log_pipe_deinit(&mpx->super);
  log_pipe_unref(&mpx->super);
  FailoverMockPipe *branches[] = { flagged, plain };
  _free_branches(branches, 2);
}

Test(logmpx, destination_failover_path_option_is_sticky_through_a_nested_multiplexer)
{
  /* named destination shape: mpx1 (the failover branch itself) -> mpx2
   * (e.g. the destination-reference wrapper) -> leaf (the actual driver).
   * mpx2's own next_hop (leaf) carries no PIF_BRANCH_DESTINATION_FAILOVER
   * of its own -- the flag must still reach it because it's inherited
   * from the incoming path_options at every dispatch (see
   * log_multiplexer_queue()), not derived solely from the immediate
   * next_hop's own flags. */
  FailoverMockPipe *leaf = _failover_mock_pipe_new(NULL, FALSE);
  LogMultiplexer *mpx2 = log_multiplexer_new(NULL);
  LogMultiplexer *mpx1 = log_multiplexer_new(NULL);

  log_pipe_init(&leaf->super);
  log_multiplexer_add_next_hop(mpx2, &leaf->super);
  cr_assert(log_pipe_init(&mpx2->super));

  mpx2->super.flags |= PIF_BRANCH_DESTINATION_FAILOVER;
  log_multiplexer_add_next_hop(mpx1, &mpx2->super);
  cr_assert(log_pipe_init(&mpx1->super));

  _queue_empty_message(&mpx1->super);

  cr_assert(leaf->last_seen_destination_failover,
            "the flag set on mpx2 must still reach its own next_hop, which carries no flag of its own");

  log_pipe_deinit(&mpx1->super);
  log_pipe_unref(&mpx1->super);
  log_pipe_deinit(&mpx2->super);
  log_pipe_unref(&mpx2->super);
  log_pipe_deinit(&leaf->super);
  log_pipe_unref(&leaf->super);
}

Test(logmpx, destination_failover_rejection_propagates_through_a_delivery_propagation_disabled_multiplexer)
{
  /* mirrors the real compiled shape for a named destination: mpx_f (the
   * junction that implements the failover chain itself, e.g. from the
   * enclosing log{} statement -- delivery_propagation TRUE, its default)
   * -> mpx_ref ("mpx(destination-reference)", which unconditionally
   * disables delivery propagation, and which mpx_f sees as a
   * PIF_BRANCH_DESTINATION_FAILOVER next_hop) -> leaf (the actual driver,
   * down). Even though mpx_ref's own delivery_propagation is FALSE, the
   * rejection must still reach mpx_f's caller: path_options->destination_failover
   * makes LogMultiplexer.queue() propagate regardless -- no re-enabling of
   * delivery_propagation anywhere in the pipe graph is needed for this. */
  FailoverMockPipe *leaf = _failover_mock_pipe_new(NULL, TRUE);
  LogMultiplexer *mpx_ref = log_multiplexer_new(NULL);
  log_multiplexer_disable_delivery_propagation(mpx_ref);

  log_pipe_init(&leaf->super);
  log_multiplexer_add_next_hop(mpx_ref, &leaf->super);
  cr_assert(log_pipe_init(&mpx_ref->super));

  mpx_ref->super.flags |= PIF_BRANCH_FINAL | PIF_BRANCH_DESTINATION_FAILOVER;

  LogMultiplexer *mpx_f = log_multiplexer_new(NULL);
  log_multiplexer_add_next_hop(mpx_f, &mpx_ref->super);
  cr_assert(log_pipe_init(&mpx_f->super));

  gboolean matched = TRUE;
  LogPathOptions path_options = LOG_PATH_OPTIONS_INIT_NOACK;
  path_options.matched = &matched;
  LogMessage *msg = log_msg_new_empty();

  log_pipe_queue(&mpx_f->super, msg, &path_options);

  cr_assert_not(matched,
                "the rejection from the down leaf must reach mpx_f's caller despite "
                "mpx_ref disabling delivery propagation");

  log_pipe_deinit(&mpx_f->super);
  log_pipe_unref(&mpx_f->super);
  log_pipe_deinit(&mpx_ref->super);
  log_pipe_unref(&mpx_ref->super);
  log_pipe_deinit(&leaf->super);
  log_pipe_unref(&leaf->super);
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
