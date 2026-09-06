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
#include "altp-session.h"
#include "gsocket.h"
#include "messages.h"
#include "metrics/metric-names.h"
#include "stats/stats-cluster-single.h"
#include "stats/stats-registry.h"
#include "mainloop.h"
#include "str-utils.h"
#include "transport/transport-factory-zlib.h"
#include "timeutils/misc.h"

#include <iv.h>

#include <errno.h>
#include <string.h>
#include <zlib.h>

/* the canonical reply texts of specification 13 */
#define ALTP_REPLY_BANNER               "220 ALTP 1.0\n"
#define ALTP_REPLY_NO_CAPABILITIES      "250 \n"
#define ALTP_REPLY_CAPABILITY_STARTTLS  "250 STARTTLS\n"
#define ALTP_REPLY_CAPABILITY_ZLIB      "250 ZLIB\n"
/* the multi-line form of 4.3: a non-final Capability line, then the final one */
#define ALTP_REPLY_CAPABILITY_BOTH      "250-STARTTLS\n250 ZLIB\n"
#define ALTP_REPLY_READY_TO_START_TLS   "250 Ready to start TLS\n"
#define ALTP_REPLY_READY_TO_START_ZLIB  "250 Ready to start ZLIB\n"
#define ALTP_REPLY_OK                   "250 OK\n"
#define ALTP_REPLY_READY                "250 Ready\n"
#define ALTP_REPLY_RECEIVED_FORMAT      "250 Received %" G_GUINT32_FORMAT "\n"
#define ALTP_REPLY_TRY_AGAIN_LATER      "421 Try again later\n"
#define ALTP_REPLY_SYNTAX_ERROR         "501 Syntax error\n"
#define ALTP_REPLY_INVALID_FRAME_HEADER "501 Invalid frame header\n"
#define ALTP_REPLY_UNKNOWN_COMMAND      "502 Unknown command\n"
#define ALTP_REPLY_NEED_SYNC            "503 Need SYNC before use this command\n"
#define ALTP_REPLY_INVALID_SESSION_ID   "504 Invalid session id\n"
#define ALTP_REPLY_STARTTLS_REQUIRED    "505 STARTTLS required\n"
#define ALTP_REPLY_ALREADY_USING_TLS    "506 Already using TLS\n"
#define ALTP_REPLY_ALREADY_USING_ZLIB   "507 Already using ZLIB\n"
#define ALTP_REPLY_INVALID_VERSION      "510 Invalid version of dialect\n"
#define ALTP_REPLY_SESSION_ALREADY_BOUND "511 Another session is already bound to this connection\n"
#define ALTP_REPLY_FRAME_TOO_LARGE      "552 Frame too large\n"

/* The `code` label values of altp_replies_total, in the order of specification
 * 13.  A counter is registered when its code is first sent, so a Receiver that
 * refuses nothing publishes no series at all.
 */
static const gchar *ALTP_REPLY_CODES[] =
{
  "421", "501", "502", "503", "504", "505", "506", "507", "510", "511", "552"
};

/* the two values of the `result` label of altp_acknowledgements_total */
enum
{
  ALTP_ACK_COMPLETE,
  ALTP_ACK_PARTIAL,
  ALTP_ACK_RESULTS,
};

/* the number of read()s a single fetch() is allowed to issue, to keep one
 * Connection from starving the others */
static const guint MAX_FETCH_COUNT = 3;

/* a Frame header is 1 to 10 digits followed by one SP (specification 8.2) */
#define ALTP_MAX_FRAME_LENGTH_DIGITS 10

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

typedef struct _AltpFetchContext
{
  const guchar **msg;
  gsize *msg_len;
  gboolean *may_read;
  LogTransportAuxData *aux;
  Bookmark *bookmark;

  /* this fetch() started in a state that asked for write-only readiness, so it
   * must produce no message (ADR-0006, see poll_prepare() below) */
  gboolean no_message;
} AltpFetchContext;

/* The metrics of one Receiver.  Every Connection registers the very same keys
 * -- the labels come from the StatsClusterKeyBuilder of the driver alone,
 * never from the Session -- so they all share reference counted counters.
 */
typedef struct _AltpMetrics
{
  /* a clone of the builder of the driver, NULL in a unit test without one; it
   * is kept until restart_with_state() names the Session Registry */
  StatsClusterKeyBuilder *kb;

  StatsClusterKey *acknowledgements_key[ALTP_ACK_RESULTS];
  StatsCounterItem *acknowledgements[ALTP_ACK_RESULTS];

  StatsClusterKey *takeovers_key;
  StatsCounterItem *takeovers;

  StatsClusterKey *sessions_refused_key;
  StatsCounterItem *sessions_refused;

  StatsClusterKey *replies_key[G_N_ELEMENTS(ALTP_REPLY_CODES)];
  StatsCounterItem *replies[G_N_ELEMENTS(ALTP_REPLY_CODES)];

  /* a gauge the Session Registry owns; the cluster is the proof that it really
   * got registered, which it does not below our stats-level() */
  StatsClusterKey *sessions_key;
  StatsCluster *sessions_cluster;
  atomic_gssize *sessions;
} AltpMetrics;

typedef struct _LogProtoAltpServer
{
  LogProtoServer super;

  AltpReceiverOptions options;
  AltpReceiverState state;
  /* where SENDING_REPLY returns to once out_buf has been written */
  AltpReceiverState next_state;

  /* the receiver context of the driver, dropped once restart_with_state()
   * handed us the Session Registry: it dies with the options of the driver,
   * which a configuration reload recreates under this Connection */
  AltpReceiverContext *context;
  /* the Session Registry of the Receiver, one reference held */
  AltpSessionRegistry *registry;

  /* the Session bound to this Connection (7.2); the id is kept so that a later
   * SYNC with another one can be refused with 511 */
  AltpSessionRecord *record;
  gchar *session_id;
  AltpSessionOwner owner;

  /* an acknowledgement was sent, so the Sender's next command line resets the
   * counters (see _process_command_line()) */
  gboolean ack_sent;
  /* set by the acknowledgement timer on the main thread, read by fetch(),
   * which may run on an I/O worker thread */
  gint ack_timed_out;
  /* the acknowledgement timeout of specification 11, armed in
   * AWAITING_DURABILITY only; the idle timer of the LogReader closes the
   * Connection and cannot serve */
  struct iv_timer ack_timer;

  /* the TLS handshake starts once the reply being written is flushed (6.1) */
  gboolean start_tls_after_reply;
  /* and so does the switch to the zlib stream, in both directions (6.2) */
  gboolean start_zlib_after_reply;
  /* STARTTLS succeeded here.  Neither the active transport nor the presence of
   * a TLS layer answers that: ZLIB may sit on top of TLS, and the stats
   * registration of afsocket constructs the TLS layer eagerly. */
  gboolean tls_active;

  /* the unparsed input is buffer[buffer_pos .. buffer_end) */
  guchar *buffer;
  gsize buffer_size, buffer_pos, buffer_end;
  guint fetch_counter;
  /* the declared payload length of the Frame being read in FRAME_PAYLOAD */
  gsize frame_len;

  /* the reply being written, out_buf[out_pos ..) is still unwritten */
  GString *out_buf;
  gsize out_pos;

  /* auxiliary data (peer address, timestamps, ...) of the buffered input */
  LogTransportAuxData buffer_aux;

  /* the peer of this Connection, resolved on demand by _get_peer_address() */
  gchar peer_address[MAX_SOCKADDR_STRING];

  AltpMetrics metrics;
} LogProtoAltpServer;

/****************************************************************************
 * Capabilities and the TLS policy (specification 5.3, 6.1)
 ****************************************************************************/

static gboolean
_is_tls_active(LogProtoAltpServer *self)
{
  return self->tls_active || self->super.transport_stack.active_transport == LOG_TRANSPORT_TLS;
}

/* a TLS factory on the stack is what tls() on the driver puts there */
static gboolean
_is_tls_configured(LogProtoAltpServer *self)
{
  return self->super.transport_stack.transport_factories[LOG_TRANSPORT_TLS] != NULL;
}

/* The AUTO the user gets without an explicit tls-policy() resolves to OPTIONAL
 * when the driver configured tls() and to NONE otherwise (ADR-0011). */
static AltpTlsPolicy
_resolve_tls_policy(LogProtoAltpServer *self)
{
  if (self->options.tls_policy != ALTP_TLS_POLICY_AUTO)
    return self->options.tls_policy;

  if (_is_tls_configured(self))
    return ALTP_TLS_POLICY_OPTIONAL;

  return ALTP_TLS_POLICY_NONE;
}

/* the none policy has no STARTTLS verb even with tls() on the driver (6.1) */
static gboolean
_is_starttls_available(LogProtoAltpServer *self)
{
  return _is_tls_configured(self)
         && _resolve_tls_policy(self) != ALTP_TLS_POLICY_NONE
         && !_is_tls_active(self);
}

static gboolean
_is_zlib_active(LogProtoAltpServer *self)
{
  return self->super.transport_stack.active_transport == LOG_TRANSPORT_ZLIB;
}

/* ZLIB is offered inside TLS as well: the deployments this Receiver is written
 * for run compression under encryption, which 6.2 permits a Receiver to do --
 * legacy Receivers withhold it there (Appendix B).
 */
static gboolean
_is_zlib_available(LogProtoAltpServer *self)
{
  return self->options.allow_compression && !_is_zlib_active(self);
}

/* One Capability name per line: the non-final lines carry the `250-` prefix
 * and the last one the `250 ` of an ordinary reply (4.3, 5.2). */
static const gchar *
_capability_reply(LogProtoAltpServer *self)
{
  gboolean starttls = _is_starttls_available(self);
  gboolean zlib = _is_zlib_available(self);

  if (starttls && zlib)
    return ALTP_REPLY_CAPABILITY_BOTH;
  if (starttls)
    return ALTP_REPLY_CAPABILITY_STARTTLS;
  if (zlib)
    return ALTP_REPLY_CAPABILITY_ZLIB;
  return ALTP_REPLY_NO_CAPABILITIES;
}

/****************************************************************************
 * The peer of the Connection (specification 15)
 ****************************************************************************/

/* Resolved once and kept: an address the transport put in the auxiliary data
 * wins, as a proxy protocol transport reports the real Sender there (15).
 * Only fetch() and poll_prepare() reach this, never at the same time.
 */
static const gchar *
_get_peer_address(LogProtoAltpServer *self)
{
  if (self->peer_address[0])
    return self->peer_address;

  GSockAddr *from_socket = NULL;
  GSockAddr *peer = self->buffer_aux.peer_addr;

  if (!peer)
    peer = from_socket = g_socket_get_peer_name(self->super.transport_stack.fd);

  if (peer)
    g_sockaddr_format(peer, self->peer_address, sizeof(self->peer_address), GSA_FULL);
  else
    g_strlcpy(self->peer_address, "unknown", sizeof(self->peer_address));

  g_sockaddr_unref(from_socket);

  return self->peer_address;
}

/****************************************************************************
 * The Session Registry of the Receiver
 ****************************************************************************/

static AltpSessionRegistry *
_get_registry(LogProtoAltpServer *self)
{
  if (G_UNLIKELY(!self->registry))
    {
      /* only reachable without restart_with_state(), i.e. a unit test with no
       * driver: the receiver context then names the registry itself */
      g_assert(self->context != NULL);
      self->registry = altp_session_registry_ref(altp_receiver_context_get_registry(self->context));
    }

  return self->registry;
}

/****************************************************************************
 * Metrics
 ****************************************************************************/

static StatsClusterKey *
_build_key(StatsClusterKeyBuilder *kb, const gchar *name, const gchar *label, const gchar *value)
{
  StatsClusterKey *key;

  stats_cluster_key_builder_push(kb);
  stats_cluster_key_builder_set_name(kb, name);
  if (label)
    stats_cluster_key_builder_add_label(kb, stats_cluster_label(label, value));
  key = stats_cluster_key_builder_build_single(kb);
  stats_cluster_key_builder_pop(kb);

  return key;
}

/* registered one by one, the first time the Receiver sends the code */
static void
_register_reply_counter(LogProtoAltpServer *self, guint slot)
{
  self->metrics.replies_key[slot] = _build_key(self->metrics.kb, METRIC(altp_replies_total),
                                               "code", ALTP_REPLY_CODES[slot]);
  stats_lock();
  stats_register_counter(STATS_LEVEL1, self->metrics.replies_key[slot], SC_TYPE_SINGLE_VALUE,
                         &self->metrics.replies[slot]);
  stats_unlock();
}

/* the altp_sessions gauge belongs to the registry, so this waits for one */
static void
_register_metrics(LogProtoAltpServer *self, AltpSessionRegistry *registry)
{
  if (!self->metrics.kb)
    return;

  StatsClusterKeyBuilder *kb = self->metrics.kb;

  self->metrics.acknowledgements_key[ALTP_ACK_COMPLETE] =
    _build_key(kb, METRIC(altp_acknowledgements_total), "result", "complete");
  self->metrics.acknowledgements_key[ALTP_ACK_PARTIAL] =
    _build_key(kb, METRIC(altp_acknowledgements_total), "result", "partial");
  self->metrics.takeovers_key = _build_key(kb, METRIC(altp_session_takeovers_total), NULL, NULL);
  self->metrics.sessions_refused_key = _build_key(kb, METRIC(altp_sessions_refused_total), NULL, NULL);
  self->metrics.sessions_key = _build_key(kb, METRIC(altp_sessions), NULL, NULL);
  self->metrics.sessions = altp_session_registry_get_session_count_ref(registry);

  stats_lock();
  for (gint i = 0; i < ALTP_ACK_RESULTS; i++)
    stats_register_counter(STATS_LEVEL1, self->metrics.acknowledgements_key[i], SC_TYPE_SINGLE_VALUE,
                           &self->metrics.acknowledgements[i]);
  stats_register_counter(STATS_LEVEL1, self->metrics.takeovers_key, SC_TYPE_SINGLE_VALUE,
                         &self->metrics.takeovers);
  stats_register_counter(STATS_LEVEL1, self->metrics.sessions_refused_key, SC_TYPE_SINGLE_VALUE,
                         &self->metrics.sessions_refused);
  /* the registry owns the gauge and outlives us: we hold a reference */
  self->metrics.sessions_cluster =
    stats_register_external_counter(STATS_LEVEL1, self->metrics.sessions_key, SC_TYPE_SINGLE_VALUE,
                                    self->metrics.sessions);
  stats_unlock();
}

static void
_unregister_metrics(LogProtoAltpServer *self)
{
  stats_lock();
  for (gint i = 0; i < ALTP_ACK_RESULTS; i++)
    {
      if (self->metrics.acknowledgements_key[i])
        stats_unregister_counter(self->metrics.acknowledgements_key[i], SC_TYPE_SINGLE_VALUE,
                                 &self->metrics.acknowledgements[i]);
    }
  if (self->metrics.takeovers_key)
    stats_unregister_counter(self->metrics.takeovers_key, SC_TYPE_SINGLE_VALUE, &self->metrics.takeovers);
  if (self->metrics.sessions_refused_key)
    stats_unregister_counter(self->metrics.sessions_refused_key, SC_TYPE_SINGLE_VALUE,
                             &self->metrics.sessions_refused);
  if (self->metrics.sessions_cluster)
    stats_unregister_external_counter(self->metrics.sessions_key, SC_TYPE_SINGLE_VALUE, self->metrics.sessions);
  for (guint i = 0; i < G_N_ELEMENTS(ALTP_REPLY_CODES); i++)
    {
      if (self->metrics.replies_key[i])
        stats_unregister_counter(self->metrics.replies_key[i], SC_TYPE_SINGLE_VALUE, &self->metrics.replies[i]);
    }
  stats_unlock();

  for (gint i = 0; i < ALTP_ACK_RESULTS; i++)
    stats_cluster_key_free(self->metrics.acknowledgements_key[i]);
  stats_cluster_key_free(self->metrics.takeovers_key);
  stats_cluster_key_free(self->metrics.sessions_refused_key);
  stats_cluster_key_free(self->metrics.sessions_key);
  for (guint i = 0; i < G_N_ELEMENTS(ALTP_REPLY_CODES); i++)
    stats_cluster_key_free(self->metrics.replies_key[i]);

  StatsClusterKeyBuilder *kb = self->metrics.kb;

  memset(&self->metrics, 0, sizeof(self->metrics));
  self->metrics.kb = kb;
}

/* @reply is a canonical text, so its first three octets are its reply code */
static void
_count_reply(LogProtoAltpServer *self, const gchar *reply)
{
  for (guint i = 0; i < G_N_ELEMENTS(ALTP_REPLY_CODES); i++)
    {
      if (strncmp(reply, ALTP_REPLY_CODES[i], 3) != 0)
        continue;

      if (!self->metrics.replies[i] && self->metrics.kb)
        _register_reply_counter(self, i);
      stats_counter_inc(self->metrics.replies[i]);
      return;
    }
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

/* the Receiver closes the Connection after every 5xx reply, and after the 421
 * with which it abandons a Batch (4.3, 9.3) */
static void
_reply_and_close(LogProtoAltpServer *self, const gchar *reply)
{
  _count_reply(self, reply);
  _queue_reply(self, reply, ALTP_CLOSED);
}

/* The one reply whose text is normative beyond its code (13.2).  It does not
 * reset the counters: see _process_command_line().
 */
static void
_queue_acknowledgement(LogProtoAltpServer *self, guint32 frames_acked, gboolean partial)
{
  g_string_append_printf(self->out_buf, ALTP_REPLY_RECEIVED_FORMAT, frames_acked);
  stats_counter_inc(self->metrics.acknowledgements[partial ? ALTP_ACK_PARTIAL : ALTP_ACK_COMPLETE]);
  self->ack_sent = TRUE;
  self->next_state = ALTP_COMMAND;
  self->state = ALTP_SENDING_REPLY;
}

/* Everything a peer pipelined after the STARTTLS or ZLIB command line arrived
 * on the transport the upgrade replaced, so it is discarded rather than
 * dispatched: a conforming Sender writes nothing there (6.1, 6.2), and
 * dispatching what an on-path attacker appended to the plaintext stream would
 * be the classic STARTTLS plaintext injection (CVE-2011-0411 and its kin).
 *
 * The read-ahead buffer of the transport underneath needs no such treatment:
 * only the auto-detecting server proto ever fills it, and that one never
 * constructs an ALTP Connection.
 */
static void
_discard_pipelined_input(LogProtoAltpServer *self, const gchar *command)
{
  gsize discarded = self->buffer_end - self->buffer_pos;

  if (discarded > 0)
    msg_warning("Discarding the ALTP input a Sender pipelined after its upgrade command line, "
                "those octets were sent on the transport the upgrade replaced",
                evt_tag_str("command", command),
                evt_tag_str("client", _get_peer_address(self)),
                evt_tag_int("discarded", discarded),
                evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));

  self->buffer_pos = self->buffer_end = 0;
  log_transport_aux_data_reinit(&self->buffer_aux);
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

  if (self->start_tls_after_reply)
    {
      /* the handshake begins immediately after the LF of the reply (6.1), and
       * happens inside the next read or write of the stack */
      self->start_tls_after_reply = FALSE;
      if (!log_transport_stack_switch(&self->super.transport_stack, LOG_TRANSPORT_TLS))
        {
          msg_error("Error switching the ALTP Connection to TLS",
                    evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
          *status = LPS_ERROR;
          return ALTP_CTRL_RETURN_WITH_STATUS;
        }
      self->tls_active = TRUE;
      _discard_pipelined_input(self, "STARTTLS");
      msg_debug("ALTP Connection switched to TLS",
                evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
    }

  if (self->start_zlib_after_reply)
    {
      /* Compression begins after the LF of the reply, in both directions and
       * for the life of the Connection.  The factory layers the zlib stream on
       * whichever transport is active, so a TLS Connection compresses inside
       * TLS (6.2).
       */
      self->start_zlib_after_reply = FALSE;
      log_transport_stack_add_factory(&self->super.transport_stack,
                                      transport_factory_zlib_new(Z_DEFAULT_COMPRESSION));
      if (!log_transport_stack_switch(&self->super.transport_stack, LOG_TRANSPORT_ZLIB))
        {
          msg_error("Error switching the ALTP Connection to ZLIB",
                    evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
          *status = LPS_ERROR;
          return ALTP_CTRL_RETURN_WITH_STATUS;
        }
      _discard_pipelined_input(self, "ZLIB");
      msg_debug("ALTP Connection switched to ZLIB",
                evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
    }

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

/* Make room for @needed octets at buffer_pos.  A Frame payload is capped at
 * log-msg-size(), so this never grows the buffer beyond max_msg_size. */
static void
_ensure_buffer_space(LogProtoAltpServer *self, gsize needed)
{
  if (self->buffer_size - self->buffer_pos >= needed)
    return;

  _compact_buffer(self);
  if (self->buffer_size >= needed)
    return;

  self->buffer_size = MAX(needed, self->buffer_size * 2);
  self->buffer = g_realloc(self->buffer, self->buffer_size);
}

/* TRUE if anything was read; @status carries the root cause otherwise. */
static gboolean
_fetch_input(LogProtoAltpServer *self, AltpFetchContext *ctx, LogProtoStatus *status)
{
  *status = LPS_SUCCESS;

  if (!(*ctx->may_read))
    return FALSE;

  if (self->fetch_counter++ >= MAX_FETCH_COUNT)
    return FALSE;

  _compact_buffer(self);
  if (self->buffer_end == self->buffer_size)
    {
      /* unreachable: the caller only asks for a read when there is room, see
       * _command_line_available() and _ensure_buffer_space() */
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
 * Commands (specification 4.2, 5.2, 6.1, 6.3, 7.2, 8.1)
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
_on_starttls(LogProtoAltpServer *self, gsize param_len)
{
  /* STARTTLS takes no parameters (6.1) */
  if (param_len != 0)
    {
      msg_error("Parameters supplied to the ALTP STARTTLS command");
      _reply_and_close(self, ALTP_REPLY_SYNTAX_ERROR);
      return;
    }

  if (_is_tls_active(self))
    {
      msg_error("ALTP STARTTLS requested on a Connection that is already inside TLS");
      _reply_and_close(self, ALTP_REPLY_ALREADY_USING_TLS);
      return;
    }

  if (_is_zlib_active(self))
    {
      /* TLS comes first, so no upgrade is left once ZLIB runs (6.1, 6.2) */
      msg_error("ALTP STARTTLS requested on a Connection that is already compressed");
      _reply_and_close(self, ALTP_REPLY_ALREADY_USING_ZLIB);
      return;
    }

  if (!_is_starttls_available(self))
    {
      /* no tls() on the driver, or tls-policy(none): 6.1 knows no such verb */
      msg_error("ALTP STARTTLS requested, but this Receiver does not offer it");
      _reply_and_close(self, ALTP_REPLY_UNKNOWN_COMMAND);
      return;
    }

  self->start_tls_after_reply = TRUE;
  _reply_and_continue(self, ALTP_REPLY_READY_TO_START_TLS);
}

static void
_on_zlib(LogProtoAltpServer *self, gsize param_len)
{
  /* ZLIB takes no parameters (6.2) */
  if (param_len != 0)
    {
      msg_error("Parameters supplied to the ALTP ZLIB command");
      _reply_and_close(self, ALTP_REPLY_SYNTAX_ERROR);
      return;
    }

  if (_is_zlib_active(self))
    {
      msg_error("ALTP ZLIB requested on a Connection that is already compressed");
      _reply_and_close(self, ALTP_REPLY_ALREADY_USING_ZLIB);
      return;
    }

  if (!self->options.allow_compression)
    {
      /* a Capability we do not offer is like a verb we do not implement (5.4, 6.2) */
      msg_error("ALTP ZLIB requested, but compression is not allowed on this Receiver");
      _reply_and_close(self, ALTP_REPLY_UNKNOWN_COMMAND);
      return;
    }

  /* the most-recent-EHLO rule of 5.3 binds the Sender, not us: one that asks
   * without reading our Capability list still gets what it asked for */
  self->start_zlib_after_reply = TRUE;
  _reply_and_continue(self, ALTP_REPLY_READY_TO_START_ZLIB);
}

/* 1 to 64 visible ASCII octets; we reject the out of range ones 7.1 lets us */
static gboolean
_is_valid_session_id(const gchar *param, gsize param_len)
{
  if (param_len == 0 || param_len > ALTP_MAX_SESSION_ID_LENGTH)
    return FALSE;

  for (gsize i = 0; i < param_len; i++)
    {
      if ((guchar) param[i] < 0x21 || (guchar) param[i] > 0x7e)
        return FALSE;
    }
  return TRUE;
}

/* At max-sessions(): the Session is not created and the Connection closes
 * without a reply -- 1.0 defines no code for a refused Session, and a Sender
 * treats the close as the transport error it is, backs off and reconnects
 * (4.3, 14.2), which is what a Receiver at its capacity wants.
 */
static void
_refuse_session(LogProtoAltpServer *self)
{
  stats_counter_inc(self->metrics.sessions_refused);

  /* the peer that provokes this is unauthenticated and can do it per
   * Connection, so the registry rate limits the line; every refusal is counted */
  if (altp_session_registry_should_log_refusal(_get_registry(self)))
    msg_warning("Refusing a new ALTP Session, the Receiver holds as many Session Records as "
                "max-sessions() allows",
                evt_tag_str("receiver", altp_session_registry_get_name(_get_registry(self))),
                evt_tag_str("session_id", self->session_id),
                evt_tag_str("client", _get_peer_address(self)),
                evt_tag_int("max_sessions", altp_session_registry_get_max_sessions(_get_registry(self))),
                evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));

  self->state = ALTP_CLOSED;
}

static void
_on_sync(LogProtoAltpServer *self, const gchar *param, gsize param_len)
{
  if (_resolve_tls_policy(self) == ALTP_TLS_POLICY_REQUIRED && !_is_tls_active(self))
    {
      msg_error("Refusing an ALTP Session on a plaintext Connection, the Receiver requires TLS");
      _reply_and_close(self, ALTP_REPLY_STARTTLS_REQUIRED);
      return;
    }

  if (!_is_valid_session_id(param, param_len))
    {
      msg_error("Invalid ALTP Session ID", evt_tag_mem("session_id", param ? param : "", param_len));
      _reply_and_close(self, ALTP_REPLY_INVALID_SESSION_ID);
      return;
    }

  if (self->session_id
      && (strlen(self->session_id) != param_len || memcmp(self->session_id, param, param_len) != 0))
    {
      /* one Session per Connection, for the life of it (7.2) */
      msg_error("Another ALTP Session is already bound to this Connection",
                evt_tag_str("session_id", self->session_id),
                evt_tag_mem("requested_session_id", param, param_len));
      _reply_and_close(self, ALTP_REPLY_SESSION_ALREADY_BOUND);
      return;
    }

  if (!self->record)
    {
      self->session_id = g_strndup(param, param_len);
      self->record = altp_session_registry_lookup(_get_registry(self), self->session_id);
      if (!self->record)
        {
          _refuse_session(self);
          return;
        }
    }

  /* newest connection wins: the older Connection closes unacknowledged (7.3) */
  gchar displaced_client[MAX_SOCKADDR_STRING] = "";

  if (altp_session_record_take_over(self->record, &self->owner, _get_peer_address(self),
                                    displaced_client, sizeof(displaced_client)))
    {
      msg_notice("Taking over an ALTP Session from an older Connection",
                 evt_tag_str("session_id", self->session_id),
                 evt_tag_str("client", _get_peer_address(self)),
                 evt_tag_str("displaced_client", displaced_client),
                 evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
      stats_counter_inc(self->metrics.takeovers);
    }

  guint32 frames_acked;
  if (altp_session_record_wait_for_durability(self->record, &self->owner, &frames_acked))
    {
      _queue_acknowledgement(self, frames_acked, FALSE);
    }
  else
    {
      /* the interrupted Batch of the Session is not durable yet, so the reply
       * is deferred and no other traffic is produced meanwhile (7.2) */
      msg_debug("Deferring the reply to an ALTP SYNC until the interrupted Batch is durable",
                evt_tag_str("session_id", self->session_id));
      self->state = ALTP_AWAITING_DURABILITY;
    }
}

static void
_on_data(LogProtoAltpServer *self)
{
  /* DATA is valid only after a successful SYNC on the same Connection (8.1) */
  if (!self->record)
    {
      msg_error("ALTP DATA received with no Session bound to the Connection");
      _reply_and_close(self, ALTP_REPLY_NEED_SYNC);
      return;
    }

  _queue_reply(self, ALTP_REPLY_READY, ALTP_FRAME_HEADER);
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

  if (_verb_equals(line, verb_len, "NOOP"))
    {
      /* any parameters are ignored rather than rejected (6.3) */
      _reply_and_continue(self, ALTP_REPLY_OK);
    }
  else if (_verb_equals(line, verb_len, "EHLO"))
    {
      _on_ehlo(self, param, param_len);
    }
  else if (_verb_equals(line, verb_len, "SYNC"))
    {
      _on_sync(self, param, param_len);
    }
  else if (_verb_equals(line, verb_len, "DATA"))
    {
      _on_data(self);
    }
  else if (_verb_equals(line, verb_len, "STARTTLS"))
    {
      _on_starttls(self, param_len);
    }
  else if (_verb_equals(line, verb_len, "ZLIB"))
    {
      _on_zlib(self, param_len);
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

  /* The deferred counter reset of ADR-0004, the first COMMAND row of 12.1 and
   * ordered before every other one on purpose: this command line is our only
   * evidence that the acknowledgement we sent was read, so the counters are
   * reset before the command itself is processed.  An acknowledgement lost on
   * the way therefore costs nothing rather than a duplicate: the counters
   * still stand, and the Sender's next SYNC of the Session is answered with
   * the very same count instead of resending anything.
   */
  if (self->ack_sent)
    {
      g_assert(self->record != NULL);
      altp_session_record_reset_counters(self->record);
      self->ack_sent = FALSE;
    }

  _dispatch_command(self, line, line_len);
}

/****************************************************************************
 * Frames (specification 8.2, 8.3)
 ****************************************************************************/

typedef enum
{
  /* neither a complete Frame header nor a complete terminator line yet */
  ALTP_HEADER_INCOMPLETE,
  ALTP_HEADER_FRAME,
  ALTP_HEADER_TERMINATOR,
  ALTP_HEADER_INVALID,
} AltpFrameHeaderResult;

/* Parse what stands where a Frame header or the Batch terminator is expected.
 * A leading zero is accepted as the legacy tolerance 8.2 permits.
 */
static AltpFrameHeaderResult
_parse_frame_header(LogProtoAltpServer *self, guint64 *frame_len, gsize *header_len)
{
  const guchar *start = &self->buffer[self->buffer_pos];
  gsize avail = self->buffer_end - self->buffer_pos;

  if (avail == 0)
    return ALTP_HEADER_INCOMPLETE;

  if (start[0] == '.')
    {
      /* both `.\n` and `.\r\n` terminate a Batch (8.3) */
      if (avail >= 2 && start[1] == '\n')
        {
          *header_len = 2;
          return ALTP_HEADER_TERMINATOR;
        }
      if (avail >= 3 && start[1] == '\r' && start[2] == '\n')
        {
          *header_len = 3;
          return ALTP_HEADER_TERMINATOR;
        }
      if (avail < 2 || (start[1] == '\r' && avail < 3))
        return ALTP_HEADER_INCOMPLETE;
      return ALTP_HEADER_INVALID;
    }

  gsize digits = 0;
  guint64 length = 0;

  while (digits < avail && ch_isdigit(start[digits]))
    {
      /* a digit run longer than 10 digits is not a header (8.2) */
      if (digits == ALTP_MAX_FRAME_LENGTH_DIGITS)
        return ALTP_HEADER_INVALID;
      length = length * 10 + (start[digits] - '0');
      digits++;
    }

  if (digits == 0)
    return ALTP_HEADER_INVALID;

  if (digits == avail)
    return ALTP_HEADER_INCOMPLETE;

  /* the header ends with exactly one SP, never CR, LF or any other octet */
  if (start[digits] != ' ')
    return ALTP_HEADER_INVALID;

  *frame_len = length;
  *header_len = digits + 1;
  return ALTP_HEADER_FRAME;
}

/* The largest payload we accept is log-msg-size() (8.2) */
static gsize
_get_max_frame_size(LogProtoAltpServer *self)
{
  return (gsize) self->super.options->max_msg_size;
}

/* A whole Frame or the terminator is buffered, or the input is already known
 * to be rejected, so that fetch() has something to do without reading. */
static gboolean
_frame_available(LogProtoAltpServer *self)
{
  guint64 frame_len = 0;
  gsize header_len = 0;

  switch (_parse_frame_header(self, &frame_len, &header_len))
    {
    case ALTP_HEADER_INCOMPLETE:
      return FALSE;
    case ALTP_HEADER_FRAME:
      if (frame_len > _get_max_frame_size(self))
        return TRUE;
      return self->buffer_end - self->buffer_pos - header_len >= frame_len;
    case ALTP_HEADER_TERMINATOR:
    case ALTP_HEADER_INVALID:
    default:
      return TRUE;
    }
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
_on_command(LogProtoAltpServer *self, AltpFetchContext *ctx, LogProtoStatus *status)
{
  if (_command_line_available(self))
    {
      _process_command_line(self);
      return ALTP_CTRL_NEXT_STATE;
    }

  if (!_fetch_input(self, ctx, status))
    return ALTP_CTRL_RETURN_WITH_STATUS;

  return ALTP_CTRL_NEXT_STATE;
}

static AltpStepControl
_on_frame_header(LogProtoAltpServer *self, AltpFetchContext *ctx, LogProtoStatus *status)
{
  guint64 frame_len = 0;
  gsize header_len = 0;

  switch (_parse_frame_header(self, &frame_len, &header_len))
    {
    case ALTP_HEADER_INCOMPLETE:
      if (!_fetch_input(self, ctx, status))
        return ALTP_CTRL_RETURN_WITH_STATUS;
      return ALTP_CTRL_NEXT_STATE;

    case ALTP_HEADER_TERMINATOR:
      self->buffer_pos += header_len;
      self->state = ALTP_AWAITING_DURABILITY;
      return ALTP_CTRL_NEXT_STATE;

    case ALTP_HEADER_FRAME:
      if (frame_len > _get_max_frame_size(self))
        {
          /* the payload is not read at all, the Connection closes (8.2, 8.5) */
          msg_error("ALTP Frame header declares a payload above the frame size limit",
                    evt_tag_int("frame_len", frame_len),
                    evt_tag_int("limit", _get_max_frame_size(self)));
          _reply_and_close(self, ALTP_REPLY_FRAME_TOO_LARGE);
          return ALTP_CTRL_NEXT_STATE;
        }
      if (frame_len == 0)
        {
          /* an empty payload carries no information, and 8.2 lets us reject it */
          msg_error("ALTP Frame header declares an empty payload");
          _reply_and_close(self, ALTP_REPLY_INVALID_FRAME_HEADER);
          return ALTP_CTRL_NEXT_STATE;
        }
      self->buffer_pos += header_len;
      self->frame_len = (gsize) frame_len;
      self->state = ALTP_FRAME_PAYLOAD;
      return ALTP_CTRL_NEXT_STATE;

    case ALTP_HEADER_INVALID:
    default:
      msg_error("Invalid ALTP Frame header",
                evt_tag_mem("input", &self->buffer[self->buffer_pos],
                            MIN(self->buffer_end - self->buffer_pos, (gsize) 16)));
      _reply_and_close(self, ALTP_REPLY_INVALID_FRAME_HEADER);
      return ALTP_CTRL_NEXT_STATE;
    }
}

static AltpStepControl
_on_frame_payload(LogProtoAltpServer *self, AltpFetchContext *ctx, LogProtoStatus *status)
{
  _ensure_buffer_space(self, self->frame_len);

  if (self->buffer_end - self->buffer_pos < self->frame_len)
    {
      if (!_fetch_input(self, ctx, status))
        return ALTP_CTRL_RETURN_WITH_STATUS;
      return ALTP_CTRL_NEXT_STATE;
    }

  if (ctx->no_message || !ctx->bookmark)
    {
      /* Either we promised no message from this fetch() (ADR-0006), or the ack
       * tracker has no Bookmark for us, which means the flow-control window is
       * full.  The Frame stays buffered and poll_prepare() asks for an
       * immediate fetch once a message is allowed again.
       */
      *status = LPS_SUCCESS;
      return ALTP_CTRL_RETURN_WITH_STATUS;
    }

  guint64 batch_seq;
  guint32 frame_index;

  /* frames_read counts the Frame as delivered upstream; the Bookmark carries
   * its position within the Batch, which is what turns a durability report
   * back into a prefix count (10.1) */
  altp_session_record_note_frame_read(self->record, &batch_seq, &frame_index);
  altp_session_bookmark_fill(ctx->bookmark, self->record, batch_seq, frame_index);

  /* the payload is opaque binary, returned in place: valid until the next fetch() */
  *ctx->msg = &self->buffer[self->buffer_pos];
  *ctx->msg_len = self->frame_len;
  self->buffer_pos += self->frame_len;
  self->frame_len = 0;
  self->state = ALTP_FRAME_HEADER;

  if (ctx->aux)
    log_transport_aux_data_copy(ctx->aux, &self->buffer_aux);

  *status = LPS_SUCCESS;
  return ALTP_CTRL_RETURN_WITH_STATUS;
}

/* The abandon row of the acknowledgement timeout in 12.1, which is what
 * ack-timeout-action(close) -- the default -- does on expiry.
 *
 * A partial acknowledgement would be the alternative, but this Receiver cannot
 * discard the Frames such an acknowledgement disowns: nothing revokes a
 * message already queued for a destination, so the Sender would resend Frames
 * we still hold, one duplicate per expiry for as long as the destination is
 * down.  Abandoning the Batch keeps exactly one copy of every Frame instead:
 * every counter stands, so the Sender's next SYNC of the Session is deferred
 * until the Frames we already read are durable and it resends nothing
 * (ADR-0009).
 *
 * The acknowledgement timer is left alone: it belongs to the main thread while
 * this may run on an I/O worker, and the poll_prepare() of the state we leave
 * to -- failing that, free() -- unregisters it.
 */
static AltpStepControl
_abandon_batch(LogProtoAltpServer *self)
{
  guint32 frames_read, frames_acked;

  altp_session_record_get_counters(self->record, &frames_read, &frames_acked);
  g_assert(frames_acked <= frames_read);
  /* only the wait ends, the counters and the ownership stay ours */
  altp_session_record_abandon(self->record, &self->owner);

  msg_notice("Abandoning an ALTP Batch, the acknowledgement timeout expired before it became durable; "
             "nothing is acknowledged and the Sender resends nothing, its next SYNC of this Session is "
             "deferred until the Frames are durable",
             evt_tag_str("session_id", self->session_id),
             evt_tag_str("client", _get_peer_address(self)),
             evt_tag_int("frames_read", frames_read),
             evt_tag_int("frames_acked", frames_acked),
             evt_tag_int("ack_timeout", self->options.ack_timeout),
             evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));

  _reply_and_close(self, ALTP_REPLY_TRY_AGAIN_LATER);
  return ALTP_CTRL_NEXT_STATE;
}

static AltpStepControl
_on_awaiting_durability(LogProtoAltpServer *self, LogProtoStatus *status)
{
  guint32 frames_acked;
  gboolean partial = FALSE;

  g_assert(self->record != NULL);

  if (g_atomic_int_get(&self->ack_timed_out))
    {
      g_atomic_int_set(&self->ack_timed_out, 0);

      if (self->options.ack_timeout_action == ALTP_ACK_TIMEOUT_ACTION_CLOSE)
        return _abandon_batch(self);

      /* acknowledge the prefix durable at this moment and disown the Frames
       * beyond it by setting frames_read := frames_acked (9.3, 12.1) */
      frames_acked = altp_session_record_acknowledge(self->record, &self->owner, TRUE);
      msg_notice("Acknowledging an ALTP Batch partially, the acknowledgement timeout expired",
                 evt_tag_str("session_id", self->session_id),
                 evt_tag_int("frames_acked", frames_acked));
      partial = TRUE;
    }
  else if (altp_session_record_wait_for_durability(self->record, &self->owner, &frames_acked))
    {
      frames_acked = altp_session_record_acknowledge(self->record, &self->owner, FALSE);
    }
  else
    {
      /* nothing is parsed while we wait and the input stays buffered (12.1);
       * the LogReader suspends us until we are woken up */
      *status = LPS_SUCCESS;
      return ALTP_CTRL_RETURN_WITH_STATUS;
    }

  _queue_acknowledgement(self, frames_acked, partial);
  return ALTP_CTRL_NEXT_STATE;
}

static AltpStepControl
_on_closed(LogProtoAltpServer *self, LogProtoStatus *status)
{
  /* terminal: report the end of input so that the LogReader closes us (12.1) */
  *status = LPS_EOF;
  return ALTP_CTRL_RETURN_WITH_STATUS;
}

/* a newer Connection took the Session over: close unacknowledged (7.3) */
static void
_handle_displacement(LogProtoAltpServer *self)
{
  if (self->state == ALTP_CLOSED || !self->record)
    return;

  if (!altp_session_record_is_displaced(self->record, &self->owner))
    return;

  gchar displacing_client[MAX_SOCKADDR_STRING] = "";

  altp_session_record_get_owner_peer_address(self->record, displacing_client, sizeof(displacing_client));
  msg_notice("Closing an ALTP Connection displaced by a newer Connection of the same Session",
             evt_tag_str("session_id", self->session_id),
             evt_tag_str("client", _get_peer_address(self)),
             evt_tag_str("displacing_client", displacing_client),
             evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
  g_string_truncate(self->out_buf, 0);
  self->out_pos = 0;
  self->state = ALTP_CLOSED;
}

static AltpStepControl
_step_state_machine(LogProtoAltpServer *self, AltpFetchContext *ctx, LogProtoStatus *status)
{
  switch (self->state)
    {
    case ALTP_GREETING:
      return _on_greeting(self);

    case ALTP_COMMAND:
      return _on_command(self, ctx, status);

    case ALTP_FRAME_HEADER:
      return _on_frame_header(self, ctx, status);

    case ALTP_FRAME_PAYLOAD:
      return _on_frame_payload(self, ctx, status);

    case ALTP_AWAITING_DURABILITY:
      return _on_awaiting_durability(self, status);

    case ALTP_SENDING_REPLY:
      return _flush_reply(self, status);

    case ALTP_CLOSED:
      return _on_closed(self, status);

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
  AltpFetchContext ctx =
  {
    .msg = msg,
    .msg_len = msg_len,
    .may_read = may_read,
    .aux = aux,
    .bookmark = bookmark,
    .no_message = self->state == ALTP_GREETING
    || self->state == ALTP_SENDING_REPLY
    || self->state == ALTP_AWAITING_DURABILITY
    || self->state == ALTP_CLOSED,
  };

  _ensure_buffer(self);
  self->fetch_counter = 0;
  _handle_displacement(self);

  while (_step_state_machine(self, &ctx, &status) != ALTP_CTRL_RETURN_WITH_STATUS)
    ;

  return status;
}

/****************************************************************************
 * The acknowledgement timeout (specification 11)
 ****************************************************************************/

/* the fetch() that follows the wakeup acknowledges the durable prefix */
static void
_ack_timer_expired(gpointer cookie)
{
  LogProtoAltpServer *self = (LogProtoAltpServer *) cookie;

  msg_debug("The acknowledgement timeout of an ALTP Batch expired",
            evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd),
            evt_tag_int("ack_timeout", self->options.ack_timeout));

  g_atomic_int_set(&self->ack_timed_out, 1);
  log_proto_server_wakeup_cb_call(&self->super.wakeup_callback);
}

void
log_proto_altp_server_fire_ack_timeout(LogProtoServer *s)
{
  LogProtoAltpServer *self = (LogProtoAltpServer *) s;

  if (iv_timer_registered(&self->ack_timer))
    iv_timer_unregister(&self->ack_timer);
  _ack_timer_expired(self);
}

static void
_update_ack_timer(LogProtoAltpServer *self, gboolean want_armed)
{
  /* an iv_timer belongs to the thread that registered it, while fetch() may
   * run on an I/O worker */
  main_loop_assert_main_thread();

  if (want_armed == !!iv_timer_registered(&self->ack_timer))
    return;

  if (!want_armed)
    {
      iv_timer_unregister(&self->ack_timer);
      return;
    }

  iv_validate_now();
  self->ack_timer.expires = iv_now;
  timespec_add_msec(&self->ack_timer.expires, (gint64) self->options.ack_timeout * 1000);
  iv_timer_register(&self->ack_timer);
}

/****************************************************************************
 * poll_prepare
 ****************************************************************************/

/* the COMMAND (idle) timeout of specification 11; idle-timeout(0) disables it */
static gint
_get_idle_timeout(LogProtoAltpServer *self)
{
  if (self->super.options->idle_timeout >= 0)
    return self->super.options->idle_timeout;
  return ALTP_DEFAULT_IDLE_TIMEOUT;
}

/* AWAITING_DURABILITY is left by exactly one acknowledgement (12.1) */
static gboolean
_is_acknowledgement_due(LogProtoAltpServer *self)
{
  if (g_atomic_int_get(&self->ack_timed_out))
    return TRUE;

  return self->record && altp_session_record_is_batch_durable(self->record);
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

  _handle_displacement(self);

  gboolean acknowledgement_due = self->state == ALTP_AWAITING_DURABILITY && _is_acknowledgement_due(self);
  _update_ack_timer(self, self->state == ALTP_AWAITING_DURABILITY && !acknowledgement_due);

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
      proto_cond = G_IO_IN;
      fetch_now = _frame_available(self);
      break;

    case ALTP_FRAME_PAYLOAD:
      proto_cond = G_IO_IN;
      fetch_now = self->buffer_end - self->buffer_pos >= self->frame_len;
      break;

    case ALTP_AWAITING_DURABILITY:
      if (!acknowledgement_due)
        {
          /* the Session Record or the acknowledgement timer wakes us up */
          return LPPA_SUSPEND;
        }
      /* the acknowledgement has to be written even though the Frames it
       * acknowledges still occupy the flow-control window (ADR-0006) */
      proto_cond = G_IO_OUT;
      break;

    case ALTP_CLOSED:
      /* Write only for the very reason GREETING and SENDING_REPLY are: the
       * window of a displaced Connection stays exhausted as long as its Frames
       * are in the pipeline, so it would never get to close if it asked for an
       * immediate fetch (ADR-0006).
       */
      proto_cond = G_IO_OUT;
      break;

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

/****************************************************************************
 * Construction
 ****************************************************************************/

/* The LogReader calls this once the transport stack of the Connection is
 * complete, which is the earliest a Receiver can tell whether tls() was
 * configured: the policy lives in our options, the TLS factory on the stack.
 * A refusal here fails the Connection, not the whole configuration.
 */
static gboolean
log_proto_altp_server_validate_options(LogProtoServer *s)
{
  LogProtoAltpServer *self = (LogProtoAltpServer *) s;
  AltpTlsPolicy policy = self->options.tls_policy;

  if ((policy == ALTP_TLS_POLICY_REQUIRED || policy == ALTP_TLS_POLICY_OPTIONAL) && !_is_tls_configured(self))
    {
      msg_error("The ALTP tls-policy() of this source needs a tls() block on the driver",
                evt_tag_str("tls_policy", policy == ALTP_TLS_POLICY_REQUIRED ? "required" : "optional"),
                evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
      return FALSE;
    }

  return log_proto_server_validate_options_method(s);
}

static gboolean
log_proto_altp_server_restart_with_state(LogProtoServer *s, PersistState *state, const gchar *persist_name)
{
  LogProtoAltpServer *self = (LogProtoAltpServer *) s;

  /* Our factory is stateful, so afsocket hands us the PersistState and the
   * persistent name of the driver right after we are constructed, and the
   * receiver context binds its Session Registry to that name (ADR-0005).
   *
   * We take a reference on the registry and forget the context: the context
   * belongs to the options of the driver, which a configuration reload
   * destroys while this Connection lives on, whereas the registry is module
   * global and refcounted.
   */
  if (self->context)
    {
      altp_receiver_context_bind_persist_state(self->context, state, persist_name, self->options.session_expiration,
                                               self->options.max_sessions);
      self->registry = altp_session_registry_ref(altp_receiver_context_get_registry(self->context));
      self->context = NULL;

      _register_metrics(self, self->registry);
    }

  return TRUE;
}

static void
log_proto_altp_server_free(LogProtoServer *s)
{
  LogProtoAltpServer *self = (LogProtoAltpServer *) s;

  if (iv_timer_registered(&self->ack_timer))
    iv_timer_unregister(&self->ack_timer);

  /* the altp_sessions gauge points into the registry, so it goes first */
  _unregister_metrics(self);
  if (self->metrics.kb)
    stats_cluster_key_builder_free(self->metrics.kb);

  if (self->record)
    {
      /* the counters stand, so the next SYNC resumes at the right point (14.2) */
      altp_session_record_release(self->record, &self->owner);
      altp_session_record_unref(self->record);
    }
  altp_session_registry_unref(self->registry);
  g_free(self->session_id);

  g_free(self->buffer);
  g_string_free(self->out_buf, TRUE);
  log_transport_aux_data_destroy(&self->buffer_aux);

  log_proto_server_free_method(s);
}

LogProtoServer *
log_proto_altp_server_new(LogTransport *transport, const LogProtoServerOptions *options,
                          const AltpReceiverOptions *altp_options, AltpReceiverContext *context,
                          StatsClusterKeyBuilder *kb)
{
  LogProtoAltpServer *self = g_new0(LogProtoAltpServer, 1);

  log_proto_server_init(&self->super, transport, options);
  self->super.poll_prepare = log_proto_altp_server_poll_prepare;
  self->super.fetch = log_proto_altp_server_fetch;
  self->super.validate_options = log_proto_altp_server_validate_options;
  self->super.restart_with_state = log_proto_altp_server_restart_with_state;
  self->super.free_fn = log_proto_altp_server_free;

  self->options = *altp_options;
  self->context = context;
  self->state = ALTP_GREETING;
  self->out_buf = g_string_sized_new(ALTP_MAX_COMMAND_LINE);

  self->owner.wakeup = &self->super.wakeup_callback;

  IV_TIMER_INIT(&self->ack_timer);
  self->ack_timer.cookie = self;
  self->ack_timer.handler = _ack_timer_expired;

  /* the builder is owned by our caller, so a clone is what survives */
  if (kb)
    self->metrics.kb = stats_cluster_key_builder_clone(kb);

  return &self->super;
}
