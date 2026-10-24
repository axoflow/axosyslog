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
#include "logproto-text-server.h"
#include "messages.h"
#include "mainloop.h"
#include "timeutils/misc.h"

#include <string.h>

/****************************************************************************
 * multi-line-timeout()
 *
 * The timer runs on the main thread, where poll_prepare() is called before
 * every poll, while fetch() may run on an I/O worker; the two never run at
 * the same time for one connection.  The timer only wakes the reader up, the
 * deadline is checked in poll_prepare(): a line may have been consumed in
 * the meantime, which moves it.
 ****************************************************************************/

static void
_multi_line_timer_expired(gpointer cookie)
{
  LogProtoTextServer *self = (LogProtoTextServer *) cookie;

  g_atomic_int_set(&self->multi_line_timer_fired, 1);
  log_proto_server_wakeup_cb_call(&self->super.super.wakeup_callback);
}

static gboolean
_multi_line_message_pending(LogProtoTextServer *self)
{
  return self->multi_line_timeout > 0 && self->consumed_len >= 0 && !self->super.flush_partial_message;
}

static void
_multi_line_check_timeout(LogProtoTextServer *self)
{
  if (!g_atomic_int_compare_and_exchange(&self->multi_line_timer_fired, 1, 0))
    return;

  if (!_multi_line_message_pending(self))
    return;

  struct timespec now = iv_now;
  if (timespec_diff_msec(&now, &self->multi_line_pending_since) < self->multi_line_timeout)
    return;

  msg_debug("Multi-line timeout has elapsed, processing partial message",
            evt_tag_int("timeout", self->multi_line_timeout));
  log_proto_buffered_server_cue_flush(&self->super);
}

static void
_multi_line_update_timer(LogProtoTextServer *self)
{
  if (self->multi_line_timeout <= 0)
    return;

  main_loop_assert_main_thread();

  struct timespec deadline = self->multi_line_pending_since;
  gboolean pending = _multi_line_message_pending(self);

  timespec_add_msec(&deadline, self->multi_line_timeout);
  if (iv_timer_registered(&self->multi_line_timer))
    {
      if (pending && timespec_diff_nsec(&deadline, &self->multi_line_timer.expires) == 0)
        return;
      iv_timer_unregister(&self->multi_line_timer);
    }
  if (!pending)
    return;

  self->multi_line_timer.expires = deadline;
  iv_timer_register(&self->multi_line_timer);
}

void
log_proto_text_server_fire_multi_line_timeout(LogProtoServer *s)
{
  LogProtoTextServer *self = (LogProtoTextServer *) s;

  if (iv_timer_registered(&self->multi_line_timer))
    iv_timer_unregister(&self->multi_line_timer);
  _multi_line_timer_expired(self);
}

LogProtoPrepareAction
log_proto_text_server_poll_prepare_method(LogProtoServer *s, GIOCondition *cond, gint *timeout)
{
  LogProtoTextServer *self = (LogProtoTextServer *) s;
  gboolean avail;

  _multi_line_check_timeout(self);
  _multi_line_update_timer(self);

  LogProtoPrepareAction action = log_proto_buffered_server_poll_prepare(s, cond, timeout);
  if (action != LPPA_POLL_IO)
    return action;

  if (self->super.flush_partial_message)
    return LPPA_FORCE_SCHEDULE_FETCH;

  avail = (self->cached_eol_pos != 0);
  return avail ? LPPA_FORCE_SCHEDULE_FETCH : LPPA_POLL_IO;
}

static gint
log_proto_text_server_accumulate_line(LogProtoTextServer *self, const guchar *msg, gsize msg_len,
                                      gssize consumed_len)
{
  if (self->multi_line)
    {
      const guchar *segment = &msg[consumed_len + 1];
      gsize segment_len = msg_len - (segment - msg);

      return multi_line_logic_accumulate_line(self->multi_line,
                                              msg, consumed_len < 0 ? 0 : consumed_len,
                                              segment, segment_len);
    }

  return MLL_CONSUME_SEGMENT | MLL_EXTRACTED;
}

static gboolean
log_proto_text_server_try_extract(LogProtoTextServer *self, LogProtoBufferedServerState *state,
                                  const guchar *buffer_start, gsize buffer_bytes, const guchar *eol, const guchar **msg, gsize *msg_len)
{
  gint verdict;
  guint32 next_line_pos;
  guint32 next_eol_pos = 0;

  next_line_pos = eol + 1 - self->super.buffer;
  if (state->pending_buffer_end != next_line_pos)
    {
      const guchar *eom;

      /* we have some more data in the buffer, check if we have a
       * subsequent EOL there.  It indicates whether we need to
       * read further data, or the buffer already contains a
       * complete line */

      eom = self->find_eom(self->super.buffer + next_line_pos, state->pending_buffer_end - next_line_pos);
      if (eom)
        next_eol_pos = eom - self->super.buffer;
    }

  *msg_len = eol - buffer_start;
  *msg = buffer_start;

  verdict = log_proto_text_server_accumulate_line(self, *msg, *msg_len, self->consumed_len);
  if (verdict & MLL_EXTRACTED)
    {
      if (verdict & MLL_CONSUME_SEGMENT)
        {
          gint drop_length = (verdict & MLL_CONSUME_PARTIAL_AMOUNT_MASK) >> MLL_CONSUME_PARTIAL_AMOUNT_SHIFT;

          state->pending_buffer_pos = next_line_pos;
          self->cached_eol_pos = next_eol_pos;
          if (drop_length)
            *msg_len -= drop_length;
        }
      else if (verdict & MLL_REWIND_SEGMENT)
        {
          if (self->consumed_len >= 0)
            *msg_len = self->consumed_len;
          else
            *msg_len = 0;

          state->pending_buffer_pos = (buffer_start + self->consumed_len + 1) - self->super.buffer;
          self->cached_eol_pos = eol - self->super.buffer;
        }
      else
        g_assert_not_reached();
      self->consumed_len = -1;
    }
  else if (verdict & MLL_WAITING)
    {
      *msg = NULL;
      *msg_len = 0;
      if (verdict & MLL_CONSUME_SEGMENT)
        {
          gint drop_length = (verdict & MLL_CONSUME_PARTIAL_AMOUNT_MASK) >> MLL_CONSUME_PARTIAL_AMOUNT_SHIFT;

          /* NOTE: we can't partially consume a line at the middle of a
           * multi-line construct, as that would mean we would have to copy
           * stuff to a separate buffer, which we don't at the moment for
           * performance reasons.
           *
           * No implementation of the MultiLineLogic interface does that and
           * honestly I can't even see a use-case at the moment: we only use
           * MLL_CONSUME_PARTIALLY() to truncate the end of the last line
           * (when "garbage" is found).  Should we ever want to add an
           * implementation that extracts only parts of the incoming lines,
           * even in the middle, we would probably have to check the
           * interface, and validate at init() time that LogProtoTextBuffer
           * is unable to work with something that does that.
           *
           * At the moment we will violently crash if this happens with a
           * SIGABRT.
           */

          g_assert(drop_length == 0);
          self->cached_eol_pos = next_eol_pos;
          self->consumed_len = eol - buffer_start;
          self->multi_line_pending_since = iv_now;
        }
      else if (verdict & MLL_REWIND_SEGMENT)
        {
          /* when we are waiting for another line, the current one
           * can't be rewinded, so MLL_REWIND_SEGMENT is not valid */
          g_assert_not_reached();
        }
      else
        {
          g_assert_not_reached();
        }
      return FALSE;
    }
  else
    g_assert_not_reached();
  return TRUE;
}

static gboolean
log_proto_text_server_extract(LogProtoTextServer *self, LogProtoBufferedServerState *state, const guchar *buffer_start,
                              gsize buffer_bytes, const guchar *eol, const guchar **msg, gsize *msg_len)
{
  do
    {
      if (log_proto_text_server_try_extract(self, state, buffer_start, buffer_bytes, eol, msg, msg_len))
        return TRUE;
      eol = self->super.buffer + self->cached_eol_pos;
    }
  while (self->cached_eol_pos > 0);
  return FALSE;
}

static inline gboolean
_is_message_boundary(guchar ch)
{
  if (ch <= 13)
    {
      /* set bits to 1 where they are considered to be message boundaries,
       * CR, LF and NUL characters are set */
      const guint32 ch_bits = (1 << '\0') | (1 << '\n') | (1 << '\r');
      return !!(ch_bits & (1 << ch));
    }
  return FALSE;
}

static void
log_proto_text_server_trim_message_boundaries(const guchar **msg, gsize *msg_len)
{
  const guchar *msg_start = (*msg);
  const guchar *msg_end = msg_start + (*msg_len);

  /* msg_end points at the newline character. A \r or \0 may precede
   * this which should be removed from the message body */

  while ((msg_end > msg_start) && (_is_message_boundary(msg_end[-1])))
    msg_end--;
  while (msg_start < msg_end && (_is_message_boundary(msg_start[0])))
    msg_start++;
  *msg_len = msg_end - msg_start;
  *msg = msg_start;
}

static inline void
log_proto_text_server_yield_whole_buffer_as_message(LogProtoTextServer *self, LogProtoBufferedServerState *state,
                                                    const guchar *buffer_start, gsize buffer_bytes, const guchar **msg, gsize *msg_len)
{
  /* no EOL, our buffer is full, no way to move forward, return
   * everything we have in our buffer. */

  *msg = buffer_start;
  *msg_len = buffer_bytes;
  self->consumed_len = -1;
  state->pending_buffer_pos = (*msg) + (*msg_len) - self->super.buffer;
  if (self->multi_line)
    multi_line_logic_reset(self->multi_line);
}

static inline const guchar *
log_proto_text_server_locate_next_eol(LogProtoTextServer *self, LogProtoBufferedServerState *state,
                                      const guchar *buffer_start, gsize buffer_bytes)
{
  const guchar *eol;

  if (self->cached_eol_pos)
    {
      /* previous invocation was nice enough to save a cached EOL
       * pointer, no need to look it up again */

      eol = self->super.buffer + self->cached_eol_pos;
      self->cached_eol_pos = 0;
    }
  else
    {
      eol = self->find_eom(buffer_start + self->consumed_len + 1, buffer_bytes - self->consumed_len - 1);
    }
  return eol;
}

static gboolean
log_proto_text_server_message_size_too_large(LogProtoTextServer *self, gsize buffer_bytes)
{
  return buffer_bytes >= self->super.super.options->max_msg_size;
}

static inline gboolean
_fetch_msg_from_buffer(LogProtoTextServer *self, LogProtoBufferedServerState *state,
                       const guchar *buffer_start, gsize buffer_bytes,
                       const guchar **msg, gsize *msg_len)
{
  const guchar *eol = log_proto_text_server_locate_next_eol(self, state, buffer_start, buffer_bytes);

  if (!eol)
    {
      if (log_proto_text_server_message_size_too_large(self, buffer_bytes)
          || log_proto_buffered_server_is_input_closed(&self->super))
        {
          log_proto_text_server_yield_whole_buffer_as_message(self, state, buffer_start, buffer_bytes, msg, msg_len);
          goto success;
        }

      return FALSE;
    }

  if (log_proto_text_server_extract(self, state, buffer_start, buffer_bytes, eol, msg, msg_len))
    goto success;

  if (log_proto_text_server_message_size_too_large(self, buffer_bytes))
    {
      log_proto_text_server_yield_whole_buffer_as_message(self, state, buffer_start, buffer_bytes, msg, msg_len);
      goto success;
    }

  return FALSE;

success:
  log_proto_text_server_trim_message_boundaries(msg, msg_len);
  return TRUE;
}

static gboolean
log_proto_text_server_fetch_from_buffer(LogProtoBufferedServer *s, const guchar *buffer_start, gsize buffer_bytes,
                                        const guchar **msg, gsize *msg_len)
{
  LogProtoTextServer *self = (LogProtoTextServer *) s;
  LogProtoBufferedServerState *state = log_proto_buffered_server_get_state(&self->super);

  gboolean result = _fetch_msg_from_buffer(self, state, buffer_start, buffer_bytes, msg, msg_len);

  log_proto_buffered_server_put_state(&self->super);
  return result;
}

static void
log_proto_text_server_flush(LogProtoBufferedServer *s)
{
  LogProtoTextServer *self = (LogProtoTextServer *) s;
  self->consumed_len = -1;
  self->cached_eol_pos = 0;
  if (self->multi_line)
    multi_line_logic_reset(self->multi_line);
}

void
log_proto_text_server_set_multi_line(LogProtoServer *s, const MultiLineOptions *options)
{
  LogProtoTextServer *self = (LogProtoTextServer *) s;

  if (self->multi_line)
    multi_line_logic_free(self->multi_line);
  self->multi_line = options->mode != MLM_NONE ? multi_line_factory_construct(options) : NULL;
  self->multi_line_timeout = options->timeout;
}

static gboolean
log_proto_text_server_restart_with_state(LogProtoServer *s, PersistState *persist_state, const gchar *persist_name)
{
  LogProtoTextServer *self = (LogProtoTextServer *) s;

  gboolean res = log_proto_buffered_server_restart_with_state(s, persist_state, persist_name);
  if (!res)
    return FALSE;

  if (!self->super.buffer)
    return FALSE;

  LogProtoBufferedServerState *state = log_proto_buffered_server_get_state(&self->super);
  const guchar *buffer_start = self->super.buffer + state->pending_buffer_pos;
  gsize buffer_bytes = state->pending_buffer_end - state->pending_buffer_pos;

  if (buffer_bytes > 0)
    {
      const guchar *eom = self->find_eom(buffer_start, buffer_bytes);
      if (eom)
        self->cached_eol_pos = eom - self->super.buffer;
    }
  log_proto_buffered_server_put_state(&self->super);

  return TRUE;
}

void
log_proto_text_server_free(LogProtoServer *s)
{
  LogProtoTextServer *self = (LogProtoTextServer *) s;
  if (iv_timer_registered(&self->multi_line_timer))
    iv_timer_unregister(&self->multi_line_timer);
  if (self->multi_line)
    multi_line_logic_free(self->multi_line);
  log_proto_buffered_server_free_method(&self->super.super);
}

void
log_proto_text_server_init(LogProtoTextServer *self, LogTransport *transport, const LogProtoServerOptions *options)
{
  log_proto_buffered_server_init(&self->super, transport, options);
  self->super.super.poll_prepare = log_proto_text_server_poll_prepare_method;
  self->super.super.free_fn = log_proto_text_server_free;
  self->super.super.restart_with_state = log_proto_text_server_restart_with_state;
  self->super.fetch_from_buffer = log_proto_text_server_fetch_from_buffer;
  self->super.flush = log_proto_text_server_flush;
  self->find_eom = find_eom;
  self->super.stream_based = TRUE;
  self->consumed_len = -1;

  IV_TIMER_INIT(&self->multi_line_timer);
  self->multi_line_timer.cookie = self;
  self->multi_line_timer.handler = _multi_line_timer_expired;
}

LogProtoServer *
log_proto_text_server_new(LogTransport *transport, const LogProtoServerOptions *options)
{
  LogProtoTextServer *self = g_new0(LogProtoTextServer, 1);

  log_proto_text_server_init(self, transport, options);
  return &self->super.super;
}

static const guchar *
_find_nl_as_eom(const guchar *s, gsize n)
{
  return memchr(s, '\n', n);
}

LogProtoServer *
log_proto_text_with_nuls_server_new(LogTransport *transport, const LogProtoServerOptions *options)
{
  LogProtoTextServer *self = g_new0(LogProtoTextServer, 1);

  log_proto_text_server_init(self, transport, options);
  self->find_eom = _find_nl_as_eom;
  return &self->super.super;
}

static const guchar *
_find_nul_as_eom(const guchar *s, gsize n)
{
  return memchr(s, 0, n);
}

LogProtoServer *
log_proto_nul_terminated_server_new(LogTransport *transport, const LogProtoServerOptions *options)
{
  LogProtoTextServer *self = g_new0(LogProtoTextServer, 1);

  log_proto_text_server_init(self, transport, options);
  self->find_eom = _find_nul_as_eom;
  return &self->super.super;
}
