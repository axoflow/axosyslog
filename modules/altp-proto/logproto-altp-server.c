/*
 * Copyright (c) 2026 Axoflow
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

#include "logproto-altp-server.h"
#include "messages.h"
#include "str-utils.h"

#include <errno.h>
#include <string.h>

/* The canonical reply texts of specification 13.  Every 5xx reply is written
 * and then the Connection closed.
 */
#define ALTP_REPLY_BANNER               "220 ALTP 1.0\n"
#define ALTP_REPLY_NO_CAPABILITIES      "250 \n"
#define ALTP_REPLY_CAPABILITY_STARTTLS  "250 STARTTLS\n"
#define ALTP_REPLY_OK                   "250 OK\n"
#define ALTP_REPLY_SYNTAX_ERROR         "501 Syntax error\n"
#define ALTP_REPLY_UNKNOWN_COMMAND      "502 Unknown command\n"
#define ALTP_REPLY_INVALID_VERSION      "510 Invalid version of dialect\n"

/* the number of read()s a single fetch() is allowed to issue, to keep one
 * Connection from starving the others */
static const guint MAX_FETCH_COUNT = 3;

/* the Receiver states of specification 12.1, plus SENDING_REPLY, an output
 * flush state that returns to next_state once the reply has been written */
typedef enum
{
  ALTP_GREETING,
  ALTP_COMMAND,
  ALTP_FRAME_HEADER,
  ALTP_FRAME_PAYLOAD,
  ALTP_AWAITING_DURABILITY,
  ALTP_SENDING_REPLY,
  ALTP_CLOSED,
} AltpReceiverState;

typedef enum
{
  ALTP_CTRL_NEXT_STATE,
  ALTP_CTRL_RETURN_WITH_STATUS,
} AltpStepControl;

typedef struct _LogProtoAltpServer
{
  LogProtoServer super;

  AltpReceiverOptions options;
  AltpReceiverState state;
  /* where SENDING_REPLY returns to once out_buf has been written */
  AltpReceiverState next_state;

  /* the unparsed input is buffer[buffer_pos .. buffer_end) */
  guchar *buffer;
  gsize buffer_size, buffer_pos, buffer_end;
  guint fetch_counter;

  /* the reply being written, out_buf[out_pos ..) is still unwritten */
  GString *out_buf;
  gsize out_pos;

  /* auxiliary data (peer address, timestamps, ...) of the buffered input */
  LogTransportAuxData buffer_aux;
} LogProtoAltpServer;

/****************************************************************************
 * Capabilities (specification 5.3)
 ****************************************************************************/

static gboolean
_is_tls_active(LogProtoAltpServer *self)
{
  return self->super.transport_stack.active_transport == LOG_TRANSPORT_TLS;
}

/* a TLS factory on the stack is what tls() on the driver puts there */
static gboolean
_is_starttls_available(LogProtoAltpServer *self)
{
  return self->super.transport_stack.transport_factories[LOG_TRANSPORT_TLS] != NULL && !_is_tls_active(self);
}

/* ZLIB is never advertised by this Receiver, so the Capability list is at
 * most one line long and never needs the multi-line form of 4.3. */
static const gchar *
_capability_reply(LogProtoAltpServer *self)
{
  if (_is_starttls_available(self))
    return ALTP_REPLY_CAPABILITY_STARTTLS;
  return ALTP_REPLY_NO_CAPABILITIES;
}

/****************************************************************************
 * Replies
 ****************************************************************************/

static void
_queue_reply(LogProtoAltpServer *self, const gchar *reply, AltpReceiverState next_state)
{
  g_string_append(self->out_buf, reply);
  self->next_state = next_state;
  self->state = ALTP_SENDING_REPLY;
}

static void
_reply_and_continue(LogProtoAltpServer *self, const gchar *reply)
{
  _queue_reply(self, reply, ALTP_COMMAND);
}

/* every 5xx reply is followed by the Receiver closing the Connection (4.3) */
static void
_reply_and_close(LogProtoAltpServer *self, const gchar *reply)
{
  _queue_reply(self, reply, ALTP_CLOSED);
}

static AltpStepControl
_flush_reply(LogProtoAltpServer *self, LogProtoStatus *status)
{
  while (self->out_pos < self->out_buf->len)
    {
      gssize rc = log_transport_stack_write(&self->super.transport_stack,
                                            self->out_buf->str + self->out_pos,
                                            self->out_buf->len - self->out_pos);
      if (rc > 0)
        {
          self->out_pos += rc;
          continue;
        }

      if (rc < 0 && errno != EAGAIN && errno != EINTR)
        {
          msg_error("Error writing ALTP reply",
                    evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd),
                    evt_tag_error(EVT_TAG_OSERROR));
          *status = LPS_ERROR;
          return ALTP_CTRL_RETURN_WITH_STATUS;
        }

      /* a partial write or EAGAIN: stay in SENDING_REPLY, wait for writability */
      *status = LPS_SUCCESS;
      return ALTP_CTRL_RETURN_WITH_STATUS;
    }

  g_string_truncate(self->out_buf, 0);
  self->out_pos = 0;
  self->state = self->next_state;
  return ALTP_CTRL_NEXT_STATE;
}

/****************************************************************************
 * Input
 ****************************************************************************/

static void
_ensure_buffer(LogProtoAltpServer *self)
{
  if (G_LIKELY(self->buffer))
    return;

  /* Stage B: a Frame payload may be as large as log-msg-size(), so the buffer
   * will have to grow up to options->max_buffer_size there. */
  self->buffer_size = MAX((gsize) self->super.options->init_buffer_size, (gsize) ALTP_MAX_COMMAND_LINE);
  self->buffer = g_malloc(self->buffer_size);
}

static void
_compact_buffer(LogProtoAltpServer *self)
{
  if (self->buffer_pos == 0)
    return;

  memmove(self->buffer, &self->buffer[self->buffer_pos], self->buffer_end - self->buffer_pos);
  self->buffer_end -= self->buffer_pos;
  self->buffer_pos = 0;
}

/* TRUE if anything was read; @status carries the root cause otherwise. */
static gboolean
_fetch_input(LogProtoAltpServer *self, gboolean *may_read, LogProtoStatus *status)
{
  *status = LPS_SUCCESS;

  if (!(*may_read))
    return FALSE;

  if (self->fetch_counter++ >= MAX_FETCH_COUNT)
    return FALSE;

  _compact_buffer(self);
  if (self->buffer_end == self->buffer_size)
    {
      /* Only reachable with a command line longer than the buffer, which
       * _command_line_available() has already reported as available, so the
       * caller never asks for a read in that case. */
      g_assert_not_reached();
    }

  log_transport_aux_data_reinit(&self->buffer_aux);
  gssize rc = log_transport_stack_read(&self->super.transport_stack,
                                       &self->buffer[self->buffer_end], self->buffer_size - self->buffer_end,
                                       &self->buffer_aux);
  if (rc < 0)
    {
      if (errno != EAGAIN && errno != EINTR)
        {
          msg_error("Error reading ALTP input",
                    evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd),
                    evt_tag_error(EVT_TAG_OSERROR));
          *status = LPS_ERROR;
        }
      return FALSE;
    }

  if (rc == 0)
    {
      msg_trace("EOF occurred while reading ALTP input",
                evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
      *status = LPS_EOF;
      return FALSE;
    }

  self->buffer_end += rc;
  return TRUE;
}

/* A complete command line is buffered, or the buffered octets already exceed
 * the line limit, in which case no terminator is needed to reject it. */
static gboolean
_command_line_available(LogProtoAltpServer *self)
{
  gsize avail = self->buffer_end - self->buffer_pos;

  if (avail == 0)
    return FALSE;
  if (memchr(&self->buffer[self->buffer_pos], '\n', avail))
    return TRUE;
  return avail >= ALTP_MAX_COMMAND_LINE;
}

/****************************************************************************
 * Commands (specification 4.2, 5.2, 6.3)
 ****************************************************************************/

/* verbs are matched exactly: a token merely beginning with one is not one (4.2) */
static gboolean
_verb_equals(const gchar *verb, gsize verb_len, const gchar *expected)
{
  return verb_len == strlen(expected) && memcmp(verb, expected, verb_len) == 0;
}

/* version = 1*DIGIT "." 1*DIGIT (specification 16) */
static gboolean
_is_well_formed_version(const gchar *param, gsize param_len)
{
  const gchar *dot = memchr(param, '.', param_len);

  if (!dot || dot == param || dot == param + param_len - 1)
    return FALSE;

  for (gsize i = 0; i < param_len; i++)
    {
      if (param + i != dot && !ch_isdigit(param[i]))
        return FALSE;
    }
  return TRUE;
}

static void
_on_ehlo(LogProtoAltpServer *self, const gchar *param, gsize param_len)
{
  /* `EHLO`, `EHLO ` and `EHLO 1.0` all mean the 1.0 dialect (5.2) */
  if (param_len != 0 && !_verb_equals(param, param_len, "1.0"))
    {
      if (_is_well_formed_version(param, param_len))
        {
          msg_error("Unsupported ALTP dialect version requested by the Sender",
                    evt_tag_mem("version", param, param_len));
          _reply_and_close(self, ALTP_REPLY_INVALID_VERSION);
        }
      else
        {
          msg_error("Malformed version in the ALTP EHLO command",
                    evt_tag_mem("version", param, param_len));
          _reply_and_close(self, ALTP_REPLY_SYNTAX_ERROR);
        }
      return;
    }

  _reply_and_continue(self, _capability_reply(self));
}

static void
_dispatch_command(LogProtoAltpServer *self, const gchar *line, gsize line_len)
{
  const gchar *param = NULL;
  gsize param_len = 0;
  gsize verb_len = line_len;

  /* a verb takes at most one parameter, the rest of the line after one SP (4.2) */
  const gchar *sp = memchr(line, ' ', line_len);
  if (sp)
    {
      verb_len = sp - line;
      param = sp + 1;
      param_len = line_len - verb_len - 1;
    }

  /* Stage B: the deferred counter reset of the first COMMAND row of 12.1
   * runs here, before the command itself is processed, and SYNC, DATA,
   * STARTTLS and ZLIB join the verbs below.  Until then every other verb is
   * answered 502 Unknown command, as an unimplemented verb is (4.2).
   */
  if (_verb_equals(line, verb_len, "NOOP"))
    {
      /* any parameters are ignored rather than rejected (6.3) */
      _reply_and_continue(self, ALTP_REPLY_OK);
    }
  else if (_verb_equals(line, verb_len, "EHLO"))
    {
      _on_ehlo(self, param, param_len);
    }
  else
    {
      msg_error("Unknown ALTP command", evt_tag_mem("verb", line, verb_len));
      _reply_and_close(self, ALTP_REPLY_UNKNOWN_COMMAND);
    }
}

static void
_process_command_line(LogProtoAltpServer *self)
{
  const gchar *line = (const gchar *) &self->buffer[self->buffer_pos];
  gsize avail = self->buffer_end - self->buffer_pos;
  const gchar *lf = memchr(line, '\n', avail);

  if (!lf || (gsize) (lf - line) + 1 > ALTP_MAX_COMMAND_LINE)
    {
      msg_error("ALTP command line exceeds the line length limit",
                evt_tag_int("limit", ALTP_MAX_COMMAND_LINE));
      /* the Connection closes after the reply, so the rest need not be located */
      self->buffer_pos = self->buffer_end;
      _reply_and_close(self, ALTP_REPLY_SYNTAX_ERROR);
      return;
    }

  gsize line_len = lf - line;
  self->buffer_pos += line_len + 1;

  /* an optional CR immediately before the LF is accepted and ignored (4.2) */
  if (line_len > 0 && line[line_len - 1] == '\r')
    line_len--;

  _dispatch_command(self, line, line_len);
}

/****************************************************************************
 * The state machine
 ****************************************************************************/

static AltpStepControl
_on_greeting(LogProtoAltpServer *self)
{
  _queue_reply(self, ALTP_REPLY_BANNER, ALTP_COMMAND);
  return ALTP_CTRL_NEXT_STATE;
}

static AltpStepControl
_on_command(LogProtoAltpServer *self, gboolean *may_read, LogProtoStatus *status)
{
  if (_command_line_available(self))
    {
      _process_command_line(self);
      return ALTP_CTRL_NEXT_STATE;
    }

  if (!_fetch_input(self, may_read, status))
    return ALTP_CTRL_RETURN_WITH_STATUS;

  return ALTP_CTRL_NEXT_STATE;
}

static AltpStepControl
_on_closed(LogProtoAltpServer *self, LogProtoStatus *status)
{
  /* CLOSED is terminal: report the end of input so that the LogReader closes
   * the Connection (12.1) */
  *status = LPS_EOF;
  return ALTP_CTRL_RETURN_WITH_STATUS;
}

static AltpStepControl
_step_state_machine(LogProtoAltpServer *self, gboolean *may_read, LogProtoStatus *status)
{
  switch (self->state)
    {
    case ALTP_GREETING:
      return _on_greeting(self);

    case ALTP_COMMAND:
      return _on_command(self, may_read, status);

    case ALTP_SENDING_REPLY:
      return _flush_reply(self, status);

    case ALTP_CLOSED:
      return _on_closed(self, status);

    case ALTP_FRAME_HEADER:
    case ALTP_FRAME_PAYLOAD:
    case ALTP_AWAITING_DURABILITY:
      /* Stage B: Frame reading, the Batch terminator and the deferred
       * acknowledgement.  Nothing enters these states yet, as DATA and SYNC
       * are not implemented. */
      g_assert_not_reached();

    default:
      g_assert_not_reached();
    }
}

static LogProtoStatus
log_proto_altp_server_fetch(LogProtoServer *s, const guchar **msg, gsize *msg_len, gboolean *may_read,
                            LogTransportAuxData *aux, Bookmark *bookmark)
{
  LogProtoAltpServer *self = (LogProtoAltpServer *) s;
  LogProtoStatus status = LPS_SUCCESS;

  _ensure_buffer(self);
  self->fetch_counter = 0;

  /* Stage B: a completed Frame is returned here as *msg/*msg_len pointing
   * into our buffer, with frames_read incremented and @bookmark filled. */
  while (_step_state_machine(self, may_read, &status) != ALTP_CTRL_RETURN_WITH_STATUS)
    ;

  return status;
}

/* the COMMAND (idle) timeout of specification 11; idle-timeout(0) disables it */
static gint
_get_idle_timeout(LogProtoAltpServer *self)
{
  if (self->super.options->idle_timeout >= 0)
    return self->super.options->idle_timeout;
  return ALTP_DEFAULT_IDLE_TIMEOUT;
}

static LogProtoPrepareAction
log_proto_altp_server_poll_prepare(LogProtoServer *s, GIOCondition *cond, gint *timeout)
{
  LogProtoAltpServer *self = (LogProtoAltpServer *) s;
  GIOCondition proto_cond;
  gboolean fetch_now = FALSE;

  /* The idle timeout of the LogReader closes the Connection on expiry, which
   * is the COMMAND row of 12.1, so it is armed in COMMAND only; 0 keeps
   * log_proto_server_poll_prepare() from substituting idle-timeout() elsewhere.
   */
  *timeout = 0;

  switch (self->state)
    {
    case ALTP_GREETING:
    case ALTP_SENDING_REPLY:
      /* write only: honoured even with an exhausted flow-control window, and in
       * exchange fetch() returns no message from these states (ADR-0006) */
      proto_cond = G_IO_OUT;
      break;

    case ALTP_COMMAND:
      proto_cond = G_IO_IN;
      *timeout = _get_idle_timeout(self);
      fetch_now = _command_line_available(self);
      break;

    case ALTP_FRAME_HEADER:
    case ALTP_FRAME_PAYLOAD:
      /* Stage B: fetch immediately when a complete Frame is already
       * buffered. */
      proto_cond = G_IO_IN;
      break;

    case ALTP_AWAITING_DURABILITY:
      /* Stage B: the Session Record wakes us through the wakeup callback
       * once the Batch is durable, or the acknowledgement timer fires. */
      return LPPA_SUSPEND;

    case ALTP_CLOSED:
      /* let fetch() run once more, so that it can report LPS_EOF */
      return LPPA_FORCE_SCHEDULE_FETCH;

    default:
      g_assert_not_reached();
    }

  /* the transport may need the opposite direction, e.g. a TLS handshake */
  if (log_transport_stack_poll_prepare(&self->super.transport_stack, cond))
    return LPPA_FORCE_SCHEDULE_FETCH;

  if (*cond == 0)
    *cond = proto_cond;

  return fetch_now ? LPPA_FORCE_SCHEDULE_FETCH : LPPA_POLL_IO;
}

static gboolean
log_proto_altp_server_restart_with_state(LogProtoServer *s, PersistState *state, const gchar *persist_name)
{
  /* Our factory is stateful, so afsocket hands us the PersistState of the
   * configuration and the persistent name of the driver right after we are
   * constructed.
   *
   * Stage B: this is where the receiver context binds its Session Registry to
   * the persistent state, sweeping and expiring the entries of the prefix on
   * the first call and ignoring the later ones.
   */
  return TRUE;
}

static void
log_proto_altp_server_free(LogProtoServer *s)
{
  LogProtoAltpServer *self = (LogProtoAltpServer *) s;

  g_free(self->buffer);
  g_string_free(self->out_buf, TRUE);
  log_transport_aux_data_destroy(&self->buffer_aux);

  log_proto_server_free_method(s);
}

LogProtoServer *
log_proto_altp_server_new(LogTransport *transport, const LogProtoServerOptions *options,
                          const AltpReceiverOptions *altp_options, StatsClusterKeyBuilder *kb)
{
  LogProtoAltpServer *self = g_new0(LogProtoAltpServer, 1);

  log_proto_server_init(&self->super, transport, options);
  self->super.poll_prepare = log_proto_altp_server_poll_prepare;
  self->super.fetch = log_proto_altp_server_fetch;
  self->super.restart_with_state = log_proto_altp_server_restart_with_state;
  self->super.free_fn = log_proto_altp_server_free;

  self->options = *altp_options;
  self->state = ALTP_GREETING;
  self->out_buf = g_string_sized_new(ALTP_MAX_COMMAND_LINE);

  /* Stage B: register the per Receiver metrics of 10.1 with @kb. */

  return &self->super;
}
