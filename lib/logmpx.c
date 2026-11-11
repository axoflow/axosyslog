/*
 * Copyright (c) 2002-2012 Balabit
 * Copyright (c) 1998-2012 Balázs Scheidler
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

#include "logmpx.h"


void
log_multiplexer_add_next_hop(LogMultiplexer *self, LogPipe *next_hop)
{
  g_ptr_array_add(self->next_hops, next_hop);
}

void
log_multiplexer_disable_delivery_propagation(LogMultiplexer *self)
{
  self->delivery_propagation = FALSE;
}

/* the last alternative of flags(destination-failover), the next_hop tried
 * last: the last fallback branch if there's one, as those are only tried
 * once every other branch failed */
static gint
_find_failover_fallback(LogMultiplexer *self)
{
  if (!(self->super.flags & PIF_DESTINATION_FAILOVER))
    return -1;

  for (gint i = self->next_hops->len - 1; i >= 0; i--)
    {
      LogPipe *next_hop = g_ptr_array_index(self->next_hops, i);

      if (!self->fallback_exists || (next_hop->flags & PIF_BRANCH_FALLBACK))
        return i;
    }
  return -1;
}

static gboolean
log_multiplexer_init(LogPipe *s)
{
  LogMultiplexer *self = (LogMultiplexer *) s;
  gint i;

  for (i = 0; i < self->next_hops->len; i++)
    {
      LogPipe *branch_head = g_ptr_array_index(self->next_hops, i);
      LogPipe *p;

      for (p = branch_head; p; p = p->pipe_next)
        {
          branch_head->flags |= (p->flags & PIF_BRANCH_PROPERTIES);
        }

      if (branch_head->flags & PIF_BRANCH_FALLBACK)
        {
          self->fallback_exists = TRUE;
        }
    }
  self->failover_fallback = _find_failover_fallback(self);
  return TRUE;
}

static gboolean
log_multiplexer_deinit(LogPipe *self)
{
  return TRUE;
}

static gboolean
_has_multiple_arcs(LogMultiplexer *self)
{
  gint num_arcs = self->next_hops->len + (self->super.pipe_next ? 1 : 0);
  return num_arcs > 1;
}

static void
log_multiplexer_queue(LogPipe *s, LogMessage *msg, const LogPathOptions *path_options)
{
  LogMultiplexer *self = (LogMultiplexer *) s;
  gint i;
  gboolean matched;
  LogPathOptions local_path_options;
  gboolean delivered = FALSE;
  gint fallback;

  log_path_options_push_junction(&local_path_options, &matched, path_options);

  /* destinations may only reject while there is an alternative left to try */
  if (self->super.flags & PIF_DESTINATION_FAILOVER)
    local_path_options.destination_failover = TRUE;

  if (_has_multiple_arcs(self))
    {
      /* if we are delivering to multiple branches, we need to sync the
       * filterx state with our message and also need to make everything
       * write protected so that changes in those branches don't overwrite
       * data we still need */

      filterx_eval_prepare_for_fork(path_options->filterx_context, &msg, path_options);
      log_msg_write_protect(msg);
    }
  for (fallback = 0; (fallback == 0) || (fallback == 1 && self->fallback_exists && !delivered); fallback++)
    {
      for (i = 0; i < self->next_hops->len; i++)
        {
          LogPipe *next_hop = g_ptr_array_index(self->next_hops, i);

          if (G_UNLIKELY(fallback == 0 && (next_hop->flags & PIF_BRANCH_FALLBACK) != 0))
            {
              continue;
            }
          else if (G_UNLIKELY(fallback && (next_hop->flags & PIF_BRANCH_FALLBACK) == 0))
            {
              continue;
            }

          /* the last alternative buffers, unless an enclosing
           * flags(destination-failover) still has alternatives of its own */
          if (G_UNLIKELY(i == self->failover_fallback))
            local_path_options.destination_failover = path_options->destination_failover;

          /* once a next_hop accepted the message, it is delivered: the
           * drivers of a destination are equivalent, the rest of them
           * buffer instead of rejecting, failover is decided above us */
          if (delivered)
            local_path_options.destination_failover = FALSE;

          matched = TRUE;
          log_msg_add_ack(msg, &local_path_options);
          log_pipe_queue(next_hop, log_msg_ref(msg), &local_path_options);

          if (matched)
            {
              delivered = TRUE;
              if (G_UNLIKELY(next_hop->flags & PIF_BRANCH_FINAL))
                break;
            }
        }
    }

  /* NOTE: non of our multiplexed next-hops delivered this message, let's
   * propagate this result.  But only if we don't have a "next".  If we do,
   * that would be responsible for doing the same, for instance if it is a
   * filter.
   *
   * There are three distinct cases where LogMultiplexer is used:
   *
   *   1) at the tail of a LogSource, where the multiplexer is used to
   *   dispatch messages along all log statements that reference the
   *   specific source.  In this case pipe_next is NULL, only next_hops are
   *   used to connect parallel branches
   *
   *   2) as the head element of a junction, in this case pipe_next is NULL,
   *   next_hops contain the branches of the junction.  Subsequent LogPipes
   *   in the current sequence are attached at the "join_pipe" which is
   *   connected to all parallel branches of the junction.
   *
   *   3) when we connect the logpath to destinations.  In this case
   *   next_hops contain the destinations we want to deliver to.  pipe_next
   *   is used to continue along the current logpath.
   *
   * Conceptually, the matched value we propagate to our parent logpath
   * determines if our parent considers this message matched or not matched
   * by this element.
   *
   *   - If we did match (e.g. delivered == TRUE), nothing is to be done.
   *
   *   - If we did not match (e.g.  delivered == FALSE), we may need to
   *   propagate this result to our parent by setting
   *   (*path_options->matched) to FALSE, see log_path_options_mark_unmatched().
   *
   * In the cases of 1) and 2) we perform a filtering function and we want
   * to tell our parent that we did NOT match so it can attempt another
   * route. We need to set matched to FALSE;
   *
   * In the case of 3) we dispatched to one or more destinations and even if
   * those destinations drop our message on the floor, we are not interested
   * unless flags(destination-failover) is set along the path.  Normally,
   * destination-failover is not set, so "matched" will be determined by all
   * filtering elements on the log path and we are not one of them.  In case
   * we are doing failover delivery, we become a filtering element, similar
   * to 1) and 2).
   *
   * We differentiate between the various cases based on the values of
   * self->delivery_propagation and path_options->destination_failover.
   *
   *  - self->delivery_propagation is set during compilation to FALSE if we
   *    are dispatching to destinations or TRUE for all other cases.
   *
   *  - path_options->destination_failover is set by a parent junction in
   *    case flags(destination-failover) was specified, the flag is also
   *    inherited through nesting.
   */

  if (self->delivery_propagation)
    {
      /* cases 1) and 2) */
      if (!delivered)
        log_path_options_mark_unmatched(path_options);
      log_pipe_forward_msg(s, msg, path_options);
    }
  else if (G_UNLIKELY(path_options->destination_failover))
    {
      /* case 3) with failover mode */
      if (!delivered)
        {
          /* The message was rejected downstream: we need to trigger a
           * failover to the next alternative in our parent junction where
           * flags(destination-failover) was specified.  We should not
           * deliver the same message along pipe_next, we essentially need
           * to drop it just like a failing filter.
           */
          log_path_options_mark_unmatched(path_options);
          log_msg_drop(msg, path_options, AT_PROCESSED);
        }
      else
        {
          /* Our downstream destination accepted the message.  Keep this status
           * as we execute our subsequent chain along pipe_next by turning off
           * the destination_failover flag.  This means that anything we chain
           * after the first successful destination will deliver normally, they
           * can't reject the message, thus they can't trigger a failover in the
           * parent.
           */
          LogPathOptions forward_path_options;

          log_path_options_chain(&forward_path_options, path_options);
          forward_path_options.destination_failover = FALSE;
          log_pipe_forward_msg(s, msg, &forward_path_options);
        }
    }
  else
    {
      /* case 3) without failover mode */
      log_pipe_forward_msg(s, msg, path_options);
    }
}

static void
log_multiplexer_free(LogPipe *s)
{
  LogMultiplexer *self = (LogMultiplexer *) s;

  g_ptr_array_free(self->next_hops, TRUE);
  log_pipe_free_method(s);
}

static void
log_multiplexer_walk(LogPipe *s, LogPathWalkFunc func, gpointer user_data)
{
  LogMultiplexer *self = (LogMultiplexer *) s;
  for (gsize i = 0; i < self->next_hops->len; i++)
    {
      LogPipe *next_hop = (LogPipe *) g_ptr_array_index(self->next_hops, i);
      if (func(s, PIW_NEXT_HOP, next_hop, user_data))
        log_pipe_walk(next_hop, func, user_data);
    }
  return log_pipe_walk_method(s, func, user_data);
}

LogMultiplexer *
log_multiplexer_new(GlobalConfig *cfg)
{
  LogMultiplexer *self = g_new0(LogMultiplexer, 1);

  log_pipe_init_instance(&self->super, cfg);
  self->super.init = log_multiplexer_init;
  self->super.deinit = log_multiplexer_deinit;
  self->super.queue = log_multiplexer_queue;
  self->super.walk = log_multiplexer_walk;
  self->super.free_fn = log_multiplexer_free;
  self->next_hops = g_ptr_array_new();
  self->delivery_propagation = TRUE;
  log_pipe_add_info(&self->super, "multiplexer");
  return self;
}
