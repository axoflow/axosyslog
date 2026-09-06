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

#include "logproto-altp-client.h"
#include "messages.h"
#include "metrics/metric-names.h"
#include "persistable-state-header.h"
#include "stats/stats-cluster-key-builder.h"
#include "stats/stats-cluster-single.h"
#include "stats/stats-registry.h"
#include "str-format.h"
#include "transport/transport-factory-zlib.h"

#include <openssl/rand.h>

#include <errno.h>
#include <string.h>
#include <zlib.h>

/* The commands of specification 4.2 this Sender writes.  NOOP is never needed:
 * see log_proto_altp_client_flush().
 */
#define ALTP_COMMAND_EHLO      "EHLO 1.0\n"
#define ALTP_COMMAND_STARTTLS  "STARTTLS\n"
#define ALTP_COMMAND_ZLIB      "ZLIB\n"
#define ALTP_COMMAND_DATA      "DATA\n"
#define ALTP_COMMAND_SYNC_FORMAT "SYNC %s\n"
#define ALTP_BATCH_TERMINATOR  ".\n"

/* the Capability names of the two upgrades this Sender knows (5.3) */
#define ALTP_CAPABILITY_STARTTLS "STARTTLS"
#define ALTP_CAPABILITY_ZLIB     "ZLIB"

/* the normative text of the one reply a Sender parses beyond its code (13.2) */
#define ALTP_REPLY_RECEIVED_PREFIX "Received "

/* the 128 bits of entropy a Session ID needs (7.1, 15), as hex octets */
#define ALTP_SESSION_ID_OCTETS 16

/* The largest amount of unwritten output kept before post() refuses messages:
 * everything beyond it stays in the queue of the destination, which is where
 * disk-buffer() and its bounds are.
 */
#define ALTP_MAX_PENDING_OUTPUT (256 * 1024)

/* the number of read()s a single process_in() is allowed to issue */
#define ALTP_MAX_READS_PER_WAKEUP 3

/* The `code` label values of altp_sender_replies_total, from the registry of
 * specification 13.  A code outside it is handled by its class (4.3) but not
 * counted, so that a Receiver cannot make us register a series of its choosing.
 */
static const gchar *ALTP_SENDER_REPLY_CODES[] =
{
  "421", "501", "502", "503", "504", "505", "506", "507", "510", "511", "552"
};

/* The Sender states of specification 12.2.  READY -- Session established, no
 * Batch open -- is not among them: see log_proto_altp_client_flush().
 */
typedef enum
{
  ALTP_SENDER_AWAIT_BANNER,
  ALTP_SENDER_EHLO_SENT,
  ALTP_SENDER_TLS_REQUESTED,
  ALTP_SENDER_ZLIB_REQUESTED,
  ALTP_SENDER_SYNC_SENT,
  ALTP_SENDER_DATA_SENT,
  ALTP_SENDER_IN_BATCH,
  ALTP_SENDER_BATCH_CLOSED,
  ALTP_SENDER_CLOSED,
} AltpSenderState;

/* The persisted Sender state of one Session (specification 10.2).  The
 * explicit padding keeps the layout free of implicit holes, so that it is the
 * same on every ABI a persist file may travel between; the byte order is
 * recorded in the header.
 */
typedef struct _AltpSenderPersistedState
{
  PersistableStateHeader header;
  guint8 __padding1[2];
  guint32 frames_sent;
  /* NUL terminated, 1 to 64 octets of %x21-7E (7.1) */
  gchar session_id[ALTP_MAX_SESSION_ID_LENGTH + 1];
  guint8 __padding2[3];
} AltpSenderPersistedState;

G_STATIC_ASSERT(sizeof(AltpSenderPersistedState) == 76);

/* The metrics of one destination.  A LogProtoClient is handed no
 * StatsClusterKeyBuilder, so the only identification available is the
 * persistent name of the driver, which restart_with_state() brings.
 */
typedef struct _AltpSenderMetrics
{
  /* the `id` label of every series, NULL until restart_with_state() ran */
  gchar *id;

  StatsClusterKey *acknowledgements_key;
  StatsCounterItem *acknowledgements;

  StatsClusterKey *frames_rewound_key;
  StatsCounterItem *frames_rewound;

  StatsClusterKey *frames_dropped_key;
  StatsCounterItem *frames_dropped;

  StatsClusterKey *replies_key[G_N_ELEMENTS(ALTP_SENDER_REPLY_CODES)];
  StatsCounterItem *replies[G_N_ELEMENTS(ALTP_SENDER_REPLY_CODES)];
} AltpSenderMetrics;

typedef struct _LogProtoAltpClient
{
  LogProtoClient super;

  AltpSenderOptions options;
  AltpSenderState state;

  /* the Session ID presented in SYNC (7.1), generated in the constructor and
   * replaced by the persisted one when restart_with_state() runs */
  gchar session_id[ALTP_MAX_SESSION_ID_LENGTH + 1];

  /* the Frames of the open Batch fully written to the transport (10.2); it
   * comes from the persistent state, so it counts the Frames of the Batch a
   * previous Connection of this Session left open as well */
  guint32 frames_sent;

  /* the messages of the open Batch post() accepted: a Frame in @out_buf and an
   * entry of the flow control backlog each; unlike frames_sent it is local to
   * this Connection */
  guint32 frames_posted;

  /* commands, Frames and terminators in wire order; out_buf[out_pos ..) is
   * still unwritten */
  GString *out_buf;
  gsize out_pos;

  /* the offset in @out_buf just past each Frame not counted in frames_sent
   * yet, oldest first: a Frame counts as sent only once written whole (10.2) */
  GArray *frame_ends;

  /* the reply being read is in in_buf[in_pos .. in_end) */
  gchar in_buf[ALTP_MAX_REPLY_LINE];
  gsize in_pos, in_end;

  /* what the most recent EHLO reply advertised, which is the only Capability
   * list we may invoke from (5.3) */
  gboolean starttls_advertised;
  gboolean zlib_advertised;

  /* STARTTLS succeeded: the active transport cannot answer that on its own
   * once ZLIB is layered on top of TLS */
  gboolean tls_active;
  /* the unavailability of STARTTLS and of compression is said once per Connection */
  gboolean starttls_unavailable_logged;
  gboolean compression_unavailable_logged;

  /* the "<driver>.altp.sender" entry, 0 while the state is memory only */
  PersistState *persist_state;
  PersistEntryHandle persist_handle;

  AltpSenderMetrics metrics;
} LogProtoAltpClient;

/****************************************************************************
 * Metrics
 ****************************************************************************/

static StatsClusterKey *
_build_key(const gchar *id, const gchar *name, const gchar *label, const gchar *value)
{
  StatsClusterKeyBuilder *kb = stats_cluster_key_builder_new();
  StatsClusterKey *key;

  stats_cluster_key_builder_add_label(kb, stats_cluster_label("id", id));
  stats_cluster_key_builder_set_name(kb, name);
  if (label)
    stats_cluster_key_builder_add_label(kb, stats_cluster_label(label, value));
  key = stats_cluster_key_builder_build_single(kb);
  stats_cluster_key_builder_free(kb);

  return key;
}

/* registered one by one, the first time the Receiver answers with the code */
static void
_register_reply_counter(LogProtoAltpClient *self, guint slot)
{
  self->metrics.replies_key[slot] = _build_key(self->metrics.id, METRIC(altp_sender_replies_total),
                                               "code", ALTP_SENDER_REPLY_CODES[slot]);
  stats_lock();
  stats_register_counter(STATS_LEVEL1, self->metrics.replies_key[slot], SC_TYPE_SINGLE_VALUE,
                         &self->metrics.replies[slot]);
  stats_unlock();
}

/* The persistent name of an afsocket destination is
 * "<module>_connections(<identifier>)" and the identifier is what we label
 * with; anything of another shape is taken as a whole.
 */
static gchar *
_format_metrics_id(const gchar *persist_name)
{
  const gchar *open_paren = strchr(persist_name, '(');
  gsize len = strlen(persist_name);

  if (!open_paren || persist_name[len - 1] != ')')
    return g_strdup(persist_name);

  return g_strndup(open_paren + 1, len - (open_paren - persist_name) - 2);
}

static void
_register_metrics(LogProtoAltpClient *self, const gchar *persist_name)
{
  self->metrics.id = _format_metrics_id(persist_name);

  self->metrics.acknowledgements_key = _build_key(self->metrics.id, METRIC(altp_sender_acknowledgements_total),
                                                  NULL, NULL);
  self->metrics.frames_rewound_key = _build_key(self->metrics.id, METRIC(altp_sender_frames_rewound_total),
                                                NULL, NULL);
  self->metrics.frames_dropped_key = _build_key(self->metrics.id, METRIC(altp_sender_frames_dropped_total),
                                                "reason", "oversize");

  stats_lock();
  stats_register_counter(STATS_LEVEL1, self->metrics.acknowledgements_key, SC_TYPE_SINGLE_VALUE,
                         &self->metrics.acknowledgements);
  stats_register_counter(STATS_LEVEL1, self->metrics.frames_rewound_key, SC_TYPE_SINGLE_VALUE,
                         &self->metrics.frames_rewound);
  stats_register_counter(STATS_LEVEL1, self->metrics.frames_dropped_key, SC_TYPE_SINGLE_VALUE,
                         &self->metrics.frames_dropped);
  stats_unlock();
}

static void
_unregister_metrics(LogProtoAltpClient *self)
{
  stats_lock();
  if (self->metrics.acknowledgements_key)
    stats_unregister_counter(self->metrics.acknowledgements_key, SC_TYPE_SINGLE_VALUE,
                             &self->metrics.acknowledgements);
  if (self->metrics.frames_rewound_key)
    stats_unregister_counter(self->metrics.frames_rewound_key, SC_TYPE_SINGLE_VALUE, &self->metrics.frames_rewound);
  if (self->metrics.frames_dropped_key)
    stats_unregister_counter(self->metrics.frames_dropped_key, SC_TYPE_SINGLE_VALUE, &self->metrics.frames_dropped);
  for (guint i = 0; i < G_N_ELEMENTS(ALTP_SENDER_REPLY_CODES); i++)
    {
      if (self->metrics.replies_key[i])
        stats_unregister_counter(self->metrics.replies_key[i], SC_TYPE_SINGLE_VALUE, &self->metrics.replies[i]);
    }
  stats_unlock();

  stats_cluster_key_free(self->metrics.acknowledgements_key);
  stats_cluster_key_free(self->metrics.frames_rewound_key);
  stats_cluster_key_free(self->metrics.frames_dropped_key);
  for (guint i = 0; i < G_N_ELEMENTS(ALTP_SENDER_REPLY_CODES); i++)
    stats_cluster_key_free(self->metrics.replies_key[i]);

  g_free(self->metrics.id);
  memset(&self->metrics, 0, sizeof(self->metrics));
}

static void
_count_reply(LogProtoAltpClient *self, const gchar *code)
{
  if (!self->metrics.id)
    return;

  for (guint i = 0; i < G_N_ELEMENTS(ALTP_SENDER_REPLY_CODES); i++)
    {
      if (strncmp(code, ALTP_SENDER_REPLY_CODES[i], 3) != 0)
        continue;

      if (!self->metrics.replies[i])
        _register_reply_counter(self, i);
      stats_counter_inc(self->metrics.replies[i]);
      return;
    }
}

/****************************************************************************
 * The persisted Sender state (specification 10.2)
 ****************************************************************************/

static void
_persisted_state_swap_byte_order(AltpSenderPersistedState *state)
{
  state->header.big_endian = !state->header.big_endian;
  state->frames_sent = GUINT32_SWAP_LE_BE(state->frames_sent);
}

/* Bring an entry written by a CPU of the opposite byte order into ours -- the
 * very trick logproto-buffered-server.c plays. */
static void
_persisted_state_normalize_byte_order(AltpSenderPersistedState *state)
{
  if ((state->header.big_endian && G_BYTE_ORDER == G_LITTLE_ENDIAN) ||
      (!state->header.big_endian && G_BYTE_ORDER == G_BIG_ENDIAN))
    _persisted_state_swap_byte_order(state);
}

static void
_persisted_state_init(AltpSenderPersistedState *state, const gchar *session_id, guint32 frames_sent)
{
  memset(state, 0, sizeof(*state));
  state->header.version = ALTP_SENDER_PERSIST_VERSION;
  state->header.big_endian = (G_BYTE_ORDER == G_BIG_ENDIAN);
  state->frames_sent = frames_sent;
  g_strlcpy(state->session_id, session_id, sizeof(state->session_id));
}

/* Write frames_sent and the Session ID through: a map, a handful of stores and
 * an unmap, so the mapping is never held across anything that may block. */
static void
_persist_sender_state(LogProtoAltpClient *self)
{
  if (!self->persist_handle)
    return;

  AltpSenderPersistedState *state = persist_state_map_entry(self->persist_state, self->persist_handle);

  _persisted_state_init(state, self->session_id, self->frames_sent);
  persist_state_unmap_entry(self->persist_state, self->persist_handle);
}

/* 1 to 64 visible ASCII characters and nothing else (7.1) */
static gboolean
_is_valid_session_id(const gchar *session_id, gsize len)
{
  if (len < 1 || len > ALTP_MAX_SESSION_ID_LENGTH)
    return FALSE;

  for (gsize i = 0; i < len; i++)
    {
      if (session_id[i] < 0x21 || session_id[i] > 0x7e)
        return FALSE;
    }

  return TRUE;
}

static void
_generate_session_id(LogProtoAltpClient *self)
{
  guchar random_bytes[ALTP_SESSION_ID_OCTETS];

  if (RAND_bytes(random_bytes, sizeof(random_bytes)) != 1)
    {
      /* possession of the Session ID is the sole authorization to resume a
       * Session (ADR-0002), so there is no fallback to a weaker source: a
       * guessable one lets anyone take our Session over (15) */
      msg_error("Cannot generate an ALTP Session ID, the cryptographic random source failed");
      self->session_id[0] = 0;
      return;
    }

  format_hex_string(random_bytes, sizeof(random_bytes), self->session_id, sizeof(self->session_id));
}

/* Read the Sender state of @handle.  FALSE when the entry is not one of ours,
 * which 10.2 lets us treat as state loss: the caller starts a new Session.
 */
static gboolean
_load_sender_state(LogProtoAltpClient *self, PersistEntryHandle handle, gsize size, const gchar *entry_name)
{
  if (size != sizeof(AltpSenderPersistedState))
    {
      msg_warning("The persisted ALTP Sender state has an unexpected size, starting a new Session",
                  evt_tag_str("entry", entry_name),
                  evt_tag_int("size", size),
                  evt_tag_int("expected_size", sizeof(AltpSenderPersistedState)));
      return FALSE;
    }

  AltpSenderPersistedState *state = persist_state_map_entry(self->persist_state, handle);

  /* a single octet, so it is readable before the byte order of the rest is */
  if (state->header.version != ALTP_SENDER_PERSIST_VERSION)
    {
      msg_warning("The persisted ALTP Sender state has an unknown version, starting a new Session",
                  evt_tag_str("entry", entry_name),
                  evt_tag_int("version", state->header.version),
                  evt_tag_int("expected_version", ALTP_SENDER_PERSIST_VERSION));
      persist_state_unmap_entry(self->persist_state, handle);
      return FALSE;
    }

  _persisted_state_normalize_byte_order(state);

  state->session_id[sizeof(state->session_id) - 1] = 0;
  if (!_is_valid_session_id(state->session_id, strlen(state->session_id)))
    {
      msg_warning("The persisted ALTP Session ID is not a valid one, starting a new Session",
                  evt_tag_str("entry", entry_name));
      persist_state_unmap_entry(self->persist_state, handle);
      return FALSE;
    }

  g_strlcpy(self->session_id, state->session_id, sizeof(self->session_id));
  self->frames_sent = state->frames_sent;
  persist_state_unmap_entry(self->persist_state, handle);

  return TRUE;
}

static gchar *
_format_entry_name(const gchar *persist_name)
{
  return g_strdup_printf("%s.altp.sender", persist_name);
}

gboolean
log_proto_altp_client_load_persisted_state(PersistState *state, const gchar *persist_name,
                                           gchar **session_id, guint32 *frames_sent)
{
  gchar *entry_name = _format_entry_name(persist_name);
  gsize size = 0;
  guint8 entry_format_version = 0;
  gboolean result = FALSE;

  PersistEntryHandle handle = persist_state_lookup_entry(state, entry_name, &size, &entry_format_version);

  if (handle && size == sizeof(AltpSenderPersistedState))
    {
      AltpSenderPersistedState *persisted = persist_state_map_entry(state, handle);

      if (persisted->header.version == ALTP_SENDER_PERSIST_VERSION)
        {
          _persisted_state_normalize_byte_order(persisted);
          persisted->session_id[sizeof(persisted->session_id) - 1] = 0;
          if (session_id)
            *session_id = g_strdup(persisted->session_id);
          if (frames_sent)
            *frames_sent = persisted->frames_sent;
          result = TRUE;
        }
      persist_state_unmap_entry(state, handle);
    }

  g_free(entry_name);

  return result;
}

/****************************************************************************
 * Output
 ****************************************************************************/

static inline gboolean
_has_pending_output(LogProtoAltpClient *self)
{
  return self->out_pos < self->out_buf->len;
}

/* Drop the octets already written from the front of @out_buf, moving the
 * pending Frame ends with them. */
static void
_compact_out_buf(LogProtoAltpClient *self)
{
  if (self->out_pos == 0)
    return;

  g_string_erase(self->out_buf, 0, self->out_pos);
  for (guint i = 0; i < self->frame_ends->len; i++)
    g_array_index(self->frame_ends, gsize, i) -= self->out_pos;
  self->out_pos = 0;
}

static void
_append_command(LogProtoAltpClient *self, const gchar *command)
{
  _compact_out_buf(self);
  g_string_append(self->out_buf, command);
}

/* the header `<length> ` and the payload, with no terminator after it (8.2) */
static void
_append_frame(LogProtoAltpClient *self, const guchar *payload, gsize payload_len)
{
  _compact_out_buf(self);
  g_string_append_printf(self->out_buf, "%" G_GSIZE_FORMAT " ", payload_len);
  g_string_append_len(self->out_buf, (const gchar *) payload, payload_len);

  gsize frame_end = self->out_buf->len;
  g_array_append_val(self->frame_ends, frame_end);
  self->frames_posted++;
}

static void
_close_batch(LogProtoAltpClient *self)
{
  _append_command(self, ALTP_BATCH_TERMINATOR);
  self->state = ALTP_SENDER_BATCH_CLOSED;
}

/* A partially written Frame is not counted (10.2).  The increment may be
 * persisted before or after the write, as either order risks a duplicate and
 * never a loss (9.4).
 */
static void
_count_written_frames(LogProtoAltpClient *self)
{
  guint completed = 0;

  while (completed < self->frame_ends->len && g_array_index(self->frame_ends, gsize, completed) <= self->out_pos)
    completed++;

  if (completed == 0)
    return;

  g_array_remove_range(self->frame_ends, 0, completed);
  self->frames_sent += completed;
  _persist_sender_state(self);
}

/* LPS_PARTIAL means output remains, which poll_prepare() turns into G_IO_OUT. */
static LogProtoStatus
_drain_output(LogProtoAltpClient *self)
{
  while (_has_pending_output(self))
    {
      gssize rc = log_transport_stack_write(&self->super.transport_stack, self->out_buf->str + self->out_pos,
                                            self->out_buf->len - self->out_pos);
      if (rc > 0)
        {
          self->out_pos += rc;
          _count_written_frames(self);
          continue;
        }

      if (rc < 0 && errno != EAGAIN && errno != EINTR)
        {
          msg_error("Error writing to the ALTP Receiver",
                    evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd),
                    evt_tag_error(EVT_TAG_OSERROR));
          return LPS_ERROR;
        }

      return LPS_PARTIAL;
    }

  g_string_truncate(self->out_buf, 0);
  self->out_pos = 0;

  return LPS_SUCCESS;
}

static LogProtoStatus
_close_connection(LogProtoAltpClient *self, LogProtoStatus status)
{
  /* terminal: every unacknowledged Frame stays in the flow control backlog and
   * the SYNC of the next Connection reconciles it (12.2, 14.4) */
  self->state = ALTP_SENDER_CLOSED;

  return status;
}

/****************************************************************************
 * The conversation (specification 12.2)
 ****************************************************************************/

static inline gboolean
_is_tls_active(LogProtoAltpClient *self)
{
  return self->tls_active || self->super.transport_stack.active_transport == LOG_TRANSPORT_TLS;
}

static inline gboolean
_is_zlib_active(LogProtoAltpClient *self)
{
  return self->super.transport_stack.active_transport == LOG_TRANSPORT_ZLIB;
}

/* a TLS factory on the stack is what tls() on the driver puts there */
static inline gboolean
_is_tls_configured(LogProtoAltpClient *self)
{
  return self->super.transport_stack.transport_factories[LOG_TRANSPORT_TLS] != NULL;
}

/* The AUTO the user gets without an explicit tls-policy() resolves to OPTIONAL
 * when the driver configured tls() and to NONE otherwise, mirroring the
 * Receiver (ADR-0011). */
static inline AltpTlsPolicy
_resolve_tls_policy(LogProtoAltpClient *self)
{
  if (self->options.tls_policy != ALTP_TLS_POLICY_AUTO)
    return self->options.tls_policy;

  if (_is_tls_configured(self))
    return ALTP_TLS_POLICY_OPTIONAL;

  return ALTP_TLS_POLICY_NONE;
}

/* The transport stack is complete by the time the banner arrives, which is the
 * earliest a Sender can tell whether tls() was configured.
 */
static gboolean
_validate_tls_policy(LogProtoAltpClient *self)
{
  AltpTlsPolicy policy = self->options.tls_policy;

  if ((policy == ALTP_TLS_POLICY_REQUIRED || policy == ALTP_TLS_POLICY_OPTIONAL) && !_is_tls_configured(self))
    {
      msg_error("The ALTP tls-policy() of this destination needs a tls() block on the driver",
                evt_tag_str("tls_policy", policy == ALTP_TLS_POLICY_REQUIRED ? "required" : "optional"),
                evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
      return FALSE;
    }

  return TRUE;
}

static LogProtoStatus
_send_ehlo(LogProtoAltpClient *self)
{
  if (!_validate_tls_policy(self))
    return LPS_ERROR;

  /* every EHLO reply supersedes the previous one (5.2, 5.3) */
  self->starttls_advertised = FALSE;
  self->zlib_advertised = FALSE;
  _append_command(self, ALTP_COMMAND_EHLO);
  self->state = ALTP_SENDER_EHLO_SENT;

  return LPS_SUCCESS;
}

static LogProtoStatus
_send_sync(LogProtoAltpClient *self)
{
  if (!self->session_id[0])
    {
      msg_error("Cannot send an ALTP SYNC without a Session ID",
                evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
      return LPS_ERROR;
    }

  _compact_out_buf(self);
  g_string_append_printf(self->out_buf, ALTP_COMMAND_SYNC_FORMAT, self->session_id);
  self->state = ALTP_SENDER_SYNC_SENT;

  return LPS_SUCCESS;
}

static LogProtoStatus
_send_data(LogProtoAltpClient *self)
{
  _append_command(self, ALTP_COMMAND_DATA);
  self->state = ALTP_SENDER_DATA_SENT;

  return LPS_SUCCESS;
}

/* invoke what the complete EHLO reply advertises, or get on with the Session */
static LogProtoStatus
_on_capabilities(LogProtoAltpClient *self)
{
  AltpTlsPolicy tls_policy = _resolve_tls_policy(self);

  if (tls_policy != ALTP_TLS_POLICY_NONE && !_is_tls_active(self))
    {
      if (self->starttls_advertised)
        {
          _append_command(self, ALTP_COMMAND_STARTTLS);
          self->state = ALTP_SENDER_TLS_REQUESTED;

          return LPS_SUCCESS;
        }

      if (tls_policy == ALTP_TLS_POLICY_REQUIRED)
        {
          /* 6.1 demands that such a Sender aborts rather than downgrading */
          msg_error("The ALTP Receiver does not offer the STARTTLS Capability but tls-policy(required) is "
                    "configured for this destination, refusing to continue in plaintext",
                    evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
          return LPS_ERROR;
        }

      /* the opportunistic policy of 15: a downgrade a deployment that must be
       * protected forbids with tls-policy(required) on both sides */
      if (!self->starttls_unavailable_logged)
        {
          self->starttls_unavailable_logged = TRUE;
          msg_notice("The ALTP Receiver does not offer the STARTTLS Capability, continuing in plaintext",
                     evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
        }
    }

  /* TLS is settled by now, so compression is what is left to ask for, and it
   * is asked for inside TLS (6.1, 12.2) */
  if (self->options.compression && !_is_zlib_active(self))
    {
      if (self->zlib_advertised)
        {
          _append_command(self, ALTP_COMMAND_ZLIB);
          self->state = ALTP_SENDER_ZLIB_REQUESTED;

          return LPS_SUCCESS;
        }

      /* a Capability the most recent EHLO reply did not advertise MUST NOT be
       * invoked (5.3), so the Session stays uncompressed */
      if (!self->compression_unavailable_logged)
        {
          self->compression_unavailable_logged = TRUE;
          msg_notice("The ALTP Receiver does not offer the ZLIB Capability but compression() is configured for "
                     "this destination, continuing without compression",
                     evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
        }
    }

  return _send_sync(self);
}

static LogProtoStatus
_on_ready_to_start_tls(LogProtoAltpClient *self)
{
  /* the handshake begins immediately after the LF of the reply (6.1), and
   * happens inside the next read or write of the transport stack */
  if (!log_transport_stack_switch(&self->super.transport_stack, LOG_TRANSPORT_TLS))
    {
      msg_error("Error switching the ALTP Connection to TLS",
                evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
      return LPS_ERROR;
    }

  if (self->in_pos < self->in_end)
    {
      /* the octets arrived in plaintext and cannot be attributed to the TLS
       * peer, so they are discarded rather than interpreted (6.1, 15) */
      msg_warning("Discarding the ALTP input a Receiver pipelined after its STARTTLS reply, "
                  "those octets were sent in plaintext",
                  evt_tag_int("discarded", self->in_end - self->in_pos),
                  evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
      self->in_pos = self->in_end = 0;
    }

  self->tls_active = TRUE;

  msg_debug("ALTP Connection switched to TLS",
            evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));

  /* the Capabilities of the upgraded Connection come from a new EHLO (6.1) */
  return _send_ehlo(self);
}

static LogProtoStatus
_on_ready_to_start_zlib(LogProtoAltpClient *self)
{
  /* Compression begins after the LF of the reply, in both directions and for
   * the life of the Connection.  The factory layers the zlib stream on
   * whichever transport is active, so a TLS Connection compresses inside TLS
   * (6.2). */
  log_transport_stack_add_factory(&self->super.transport_stack,
                                  transport_factory_zlib_new(self->options.compression_level));

  if (!log_transport_stack_switch(&self->super.transport_stack, LOG_TRANSPORT_ZLIB))
    {
      msg_error("Error switching the ALTP Connection to ZLIB",
                evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
      return LPS_ERROR;
    }

  if (self->in_pos < self->in_end)
    {
      /* the octets were written uncompressed and are not part of the deflate
       * stream the peer started, so they are discarded (6.2) */
      msg_warning("Discarding the ALTP input a Receiver pipelined after its ZLIB reply, "
                  "those octets were sent uncompressed",
                  evt_tag_int("discarded", self->in_end - self->in_pos),
                  evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
      self->in_pos = self->in_end = 0;
    }

  msg_debug("ALTP Connection switched to ZLIB",
            evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));

  return _send_sync(self);
}

/* `Received` followed by an ASCII decimal count and nothing else (9.4, 13.2) */
static gboolean
_parse_received(const gchar *text, gsize text_len, guint32 *frames_acked)
{
  const gsize prefix_len = strlen(ALTP_REPLY_RECEIVED_PREFIX);

  if (text_len <= prefix_len || strncmp(text, ALTP_REPLY_RECEIVED_PREFIX, prefix_len) != 0)
    return FALSE;

  const gchar *digits = text + prefix_len;
  gsize digits_len = text_len - prefix_len;
  guint64 value = 0;

  /* a run longer than the range of the counter cannot be a Batch count */
  if (digits_len > 10)
    return FALSE;

  for (gsize i = 0; i < digits_len; i++)
    {
      if (!g_ascii_isdigit(digits[i]))
        return FALSE;
      value = value * 10 + (digits[i] - '0');
    }

  if (value > G_MAXUINT32)
    return FALSE;

  *frames_acked = (guint32) value;

  return TRUE;
}

/* Process `250 Received <n>`, whether it answers a SYNC or a terminator (9.4).
 */
static LogProtoStatus
_on_acknowledgement(LogProtoAltpClient *self, guint32 frames_acked)
{
  if (_has_pending_output(self))
    {
      /* the terminator is the last octet of the Batch, so this cannot answer
       * anything we finished sending: close and let the next SYNC establish
       * the count authoritatively (14.1) */
      msg_error("ALTP acknowledgement arrived before the Batch was written in full, closing the connection",
                evt_tag_int("frames_acked", frames_acked),
                evt_tag_int("pending_octets", self->out_buf->len - self->out_pos),
                evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
      return LPS_ERROR;
    }

  if (frames_acked > self->frames_sent)
    {
      /* Step 1: the excess is ignored.  It happens when we lost part of our
       * own record, and when a Batch we did read the acknowledgement of is
       * reported again by the next SYNC (9.2, ADR-0004). */
      msg_debug("The ALTP Receiver acknowledged more Frames than this Session has open, clamping",
                evt_tag_int("frames_acked", frames_acked),
                evt_tag_int("frames_sent", self->frames_sent),
                evt_tag_str("session_id", self->session_id));
      frames_acked = self->frames_sent;
    }

  guint32 frames_rewound = self->frames_sent - frames_acked;

  /* Step 2 MUST complete before step 3: had we released the acknowledged
   * Frames first and crashed, we would restart believing they were still in
   * flight, and the next SYNC -- which confirms them -- would make us release
   * that many further Frames that were never sent (9.4, 10.2). */
  self->frames_sent = 0;
  _persist_sender_state(self);

  /* Step 3: the flow control interface releases exactly the oldest entries of
   * the backlog, which is what the acknowledged Frames are. */
  if (frames_acked > 0)
    log_proto_client_msg_ack(&self->super, frames_acked);

  /* Step 4: everything the backlog still holds is rewound for resend, in its
   * original order and before any newer message.
   *
   * The rewind is unconditional, and that is what keeps the backlog and our
   * counting from drifting apart.  In the ordinary case the backlog holds
   * exactly the Frames of the open Batch, so rewinding after a complete
   * acknowledgement is a no-op.  Anything else in there -- a message a queue
   * restored while our frames_sent did not survive, one whose formatted
   * payload was empty and so never became a Frame -- is resent rather than
   * left behind to be counted against the acknowledgement of a later Batch,
   * which would release the wrong messages.  Resending risks a duplicate;
   * leaving it behind would risk a loss (10.2).
   */
  log_proto_client_msg_rewind(&self->super);
  self->frames_posted = 0;
  g_array_set_size(self->frame_ends, 0);

  stats_counter_inc(self->metrics.acknowledgements);
  stats_counter_add(self->metrics.frames_rewound, frames_rewound);

  msg_debug("ALTP Batch acknowledged",
            evt_tag_str("session_id", self->session_id),
            evt_tag_int("frames_acked", frames_acked),
            evt_tag_int("frames_rewound", frames_rewound),
            evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));

  /* the next Batch is opened at once, see log_proto_altp_client_flush() */
  return _send_data(self);
}

static LogProtoStatus
_on_malformed_reply(LogProtoAltpClient *self, const gchar *line, gsize line_len)
{
  msg_error("The ALTP Receiver sent a malformed reply, closing the connection",
            evt_tag_printf("reply", "%.*s", (gint) line_len, line),
            evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));

  return LPS_ERROR;
}

/* three ASCII digits, then SP for a final line or `-` for a non-final one,
 * then printable ASCII text (4.3, 14.1)
 */
static gboolean
_is_well_formed_reply(const gchar *line, gsize line_len)
{
  if (line_len < 4)
    return FALSE;

  if (!g_ascii_isdigit(line[0]) || !g_ascii_isdigit(line[1]) || !g_ascii_isdigit(line[2]))
    return FALSE;

  if (line[3] != ' ' && line[3] != '-')
    return FALSE;

  for (gsize i = 0; i < line_len; i++)
    {
      if (line[i] < 0x20 || line[i] > 0x7e)
        return FALSE;
    }

  return TRUE;
}

static LogProtoStatus
_on_received_reply(LogProtoAltpClient *self, const gchar *line, gsize line_len, const gchar *text, gsize text_len)
{
  guint32 frames_acked = 0;

  if (!_parse_received(text, text_len, &frames_acked))
    {
      /* a count may not be guessed: reconnect and SYNC re-establish it (9.4) */
      msg_error("The ALTP Receiver sent a malformed acknowledgement, closing the connection",
                evt_tag_printf("reply", "%.*s", (gint) line_len, line),
                evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
      return LPS_ERROR;
    }

  return _on_acknowledgement(self, frames_acked);
}

/* @line has its terminator and any CR before it stripped. */
static LogProtoStatus
_on_reply_line(LogProtoAltpClient *self, const gchar *line, gsize line_len)
{
  if (!_is_well_formed_reply(line, line_len))
    return _on_malformed_reply(self, line, line_len);

  const gchar *text = line + 4;
  gsize text_len = line_len - 4;
  gboolean continuation = (line[3] == '-');

  if (line[0] == '4')
    {
      /* transient: nothing was acknowledged and nothing disowned, so nothing
       * is resent -- the next SYNC reports what became durable (4.3, 9.3) */
      _count_reply(self, line);
      msg_notice("The ALTP Receiver reported a transient failure, closing the connection",
                 evt_tag_printf("reply", "%.*s", (gint) line_len, line),
                 evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
      return LPS_EOF;
    }

  if (line[0] == '5')
    {
      /* permanent for this command: every Frame is retained and reconciled by
       * the SYNC of the next Connection (4.3, 8.5, 14.4) */
      _count_reply(self, line);
      msg_error("The ALTP Receiver refused a command, closing the connection",
                evt_tag_printf("reply", "%.*s", (gint) line_len, line),
                evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
      return LPS_ERROR;
    }

  if (line[0] != '2')
    return _on_malformed_reply(self, line, line_len);

  /* the Capability list is the only multi-line reply of ALTP 1.0 (4.3, 5.2) */
  if (continuation && self->state != ALTP_SENDER_EHLO_SENT)
    return _on_malformed_reply(self, line, line_len);

  switch (self->state)
    {
    case ALTP_SENDER_AWAIT_BANNER:
      /* the banner text MUST NOT be parsed: a legacy Receiver greets with
       * `220 RLTP 1.0` (5.1) */
      msg_debug("The ALTP Receiver greeted us",
                evt_tag_printf("banner", "%.*s", (gint) line_len, line),
                evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
      return _send_ehlo(self);

    case ALTP_SENDER_EHLO_SENT:
      /* one name per line; an unknown one is ignored, not an error (5.3) */
      if (text_len == strlen(ALTP_CAPABILITY_STARTTLS) &&
          strncmp(text, ALTP_CAPABILITY_STARTTLS, text_len) == 0)
        self->starttls_advertised = TRUE;
      else if (text_len == strlen(ALTP_CAPABILITY_ZLIB) &&
               strncmp(text, ALTP_CAPABILITY_ZLIB, text_len) == 0)
        self->zlib_advertised = TRUE;

      if (continuation)
        return LPS_SUCCESS;

      return _on_capabilities(self);

    case ALTP_SENDER_TLS_REQUESTED:
      return _on_ready_to_start_tls(self);

    case ALTP_SENDER_ZLIB_REQUESTED:
      return _on_ready_to_start_zlib(self);

    case ALTP_SENDER_SYNC_SENT:
    case ALTP_SENDER_BATCH_CLOSED:
      return _on_received_reply(self, line, line_len, text, text_len);

    case ALTP_SENDER_DATA_SENT:
      /* `250 Ready`: the Batch is open and Frames may be written (8.1) */
      self->state = ALTP_SENDER_IN_BATCH;
      return LPS_SUCCESS;

    case ALTP_SENDER_IN_BATCH:
      /* apart from the banner a Receiver sends no unsolicited reply, so this
       * Connection is out of step with it (13.2, 14.1) */
      msg_error("The ALTP Receiver sent an unsolicited reply inside an open Batch, closing the connection",
                evt_tag_printf("reply", "%.*s", (gint) line_len, line),
                evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
      return LPS_ERROR;

    case ALTP_SENDER_CLOSED:
      return LPS_EOF;

    default:
      g_assert_not_reached();
    }
}

static LogProtoStatus
_dispatch_replies(LogProtoAltpClient *self)
{
  while (self->in_pos < self->in_end)
    {
      gchar *line = self->in_buf + self->in_pos;
      gsize available = self->in_end - self->in_pos;
      gchar *lf = memchr(line, '\n', available);

      if (!lf)
        break;

      gsize line_len = lf - line;

      self->in_pos += line_len + 1;

      /* a CR before the LF is tolerated rather than made an error (4.3) */
      if (line_len > 0 && line[line_len - 1] == '\r')
        line_len--;

      LogProtoStatus status = _on_reply_line(self, line, line_len);
      if (status != LPS_SUCCESS)
        return status;
    }

  if (self->in_pos == self->in_end)
    self->in_pos = self->in_end = 0;

  return LPS_SUCCESS;
}

/* @got_input tells the caller whether another read is worth trying. */
static LogProtoStatus
_read_input(LogProtoAltpClient *self, gboolean *got_input)
{
  *got_input = FALSE;

  if (self->in_pos > 0)
    {
      memmove(self->in_buf, self->in_buf + self->in_pos, self->in_end - self->in_pos);
      self->in_end -= self->in_pos;
      self->in_pos = 0;
    }

  if (self->in_end == sizeof(self->in_buf))
    {
      /* a conforming reply line fits in the buffer, terminator and all (4.3) */
      msg_error("The ALTP Receiver sent a reply line longer than the limit, closing the connection",
                evt_tag_int("limit", ALTP_MAX_REPLY_LINE),
                evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
      return LPS_ERROR;
    }

  gssize rc = log_transport_stack_read(&self->super.transport_stack, self->in_buf + self->in_end,
                                       sizeof(self->in_buf) - self->in_end, NULL);
  if (rc > 0)
    {
      self->in_end += rc;
      *got_input = TRUE;
      return LPS_SUCCESS;
    }

  if (rc == 0)
    {
      /* an idle close is normal termination and no error: we reconnect on
       * demand and the next SYNC reconciles (4.4, 9.5, 14.2) */
      msg_debug("The ALTP Receiver closed the connection",
                evt_tag_int("frames_sent", self->frames_sent),
                evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
      return LPS_EOF;
    }

  if (errno != EAGAIN && errno != EINTR)
    {
      msg_error("Error reading from the ALTP Receiver",
                evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd),
                evt_tag_error(EVT_TAG_OSERROR));
      return LPS_ERROR;
    }

  return LPS_SUCCESS;
}

/****************************************************************************
 * The LogProtoClient interface
 ****************************************************************************/

/* Reading a reply is what drives the whole conversation, so no separate
 * negotiation phase is needed before the writer may post messages -- post()
 * simply refuses until a Batch is open.
 */
static LogProtoStatus
log_proto_altp_client_process_in(LogProtoClient *s)
{
  LogProtoAltpClient *self = (LogProtoAltpClient *) s;
  LogProtoStatus status;

  if (self->state == ALTP_SENDER_CLOSED)
    return LPS_EOF;

  for (guint round = 0; round < ALTP_MAX_READS_PER_WAKEUP; round++)
    {
      gboolean got_input = FALSE;

      /* the input buffer holds one reply line at most, so it has to be emptied
       * before it is refilled */
      status = _dispatch_replies(self);
      if (status != LPS_SUCCESS)
        return _close_connection(self, status);

      status = _read_input(self, &got_input);
      if (status != LPS_SUCCESS)
        return _close_connection(self, status);

      if (!got_input)
        break;
    }

  status = _dispatch_replies(self);
  if (status != LPS_SUCCESS)
    return _close_connection(self, status);

  status = _drain_output(self);
  if (status == LPS_ERROR)
    return _close_connection(self, status);

  return status;
}

/* batch-size(), falling back to flush-lines() of the driver, but never more
 * than the 1000 Frames of specification 8.4 */
static guint32
_batch_frame_bound(LogProtoAltpClient *self)
{
  gint bound = self->options.batch_size;

  if (bound <= 0)
    bound = self->super.options ? self->super.options->flush_lines : 0;

  if (bound <= 0)
    return ALTP_SENDER_MAX_BATCH_FRAMES;

  return MIN((guint32) bound, ALTP_SENDER_MAX_BATCH_FRAMES);
}

/* A message above max-frame-size() is dropped rather than sent: a Receiver
 * refuses it with `552 Frame too large` and closes, so retrying it would abort
 * every later Connection too (8.5).
 *
 * Dropping releases it upstream, and the only release primitive of the flow
 * control interface acknowledges the OLDEST @n entries of the backlog.  A drop
 * is therefore positional: it is correct only while this message IS the oldest
 * entry, that is while no Frame of the open Batch is in the backlog yet.  With
 * Frames ahead of it, releasing one entry would release the first of THEM and
 * leave this message behind to be counted against the acknowledgement of the
 * Batch, releasing the wrong messages from there on.
 *
 * A drop in the middle of a Batch is therefore refused rather than performed:
 * the writer rewinds the message and ends its pass, flush() closes the Batch,
 * and processing its acknowledgement empties the backlog (9.4).  The message
 * is popped again on the next pass as the first message of a fresh Batch, so
 * an oversized message costs at most one extra Batch boundary.
 */
static LogProtoStatus
_drop_oversized_message(LogProtoAltpClient *self, guchar *msg, gsize msg_len, gboolean *consumed)
{
  if (self->frames_posted > 0)
    return LPS_SUCCESS;

  msg_error("Message is larger than max-frame-size() of this ALTP destination, dropping it",
            evt_tag_int("size", msg_len),
            evt_tag_int("max_frame_size", self->options.max_frame_size),
            evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
  stats_counter_inc(self->metrics.frames_dropped);

  log_proto_client_msg_ack(&self->super, 1);
  g_free(msg);
  *consumed = TRUE;

  return LPS_SUCCESS;
}

/* Take one message into the open Batch.
 *
 * A refusal -- *consumed FALSE with LPS_SUCCESS -- makes the LogWriter rewind
 * that one message and end the pass, which is what is wanted whenever no Frame
 * may be written.  LPS_PARTIAL is never returned: the LogWriter remembers it
 * and rewinds the WHOLE backlog on reopen, which for a Session that retains
 * its Frames until they are acknowledged would resend the whole open Batch on
 * every reconnect.
 */
static LogProtoStatus
log_proto_altp_client_post(LogProtoClient *s, LogMessage *logmsg, guchar *msg, gsize msg_len, gboolean *consumed)
{
  LogProtoAltpClient *self = (LogProtoAltpClient *) s;

  *consumed = FALSE;

  /* a Frame may only be written between `250 Ready` and the terminator (8.1,
   * 12.2); in every other state the message waits in the queue */
  if (self->state != ALTP_SENDER_IN_BATCH)
    return LPS_SUCCESS;

  if (_drain_output(self) == LPS_ERROR)
    return _close_connection(self, LPS_ERROR);

  if (msg_len > (gsize) self->options.max_frame_size)
    return _drop_oversized_message(self, msg, msg_len, consumed);

  if (self->out_buf->len - self->out_pos >= ALTP_MAX_PENDING_OUTPUT)
    return LPS_SUCCESS;

  _append_frame(self, msg, msg_len);
  g_free(msg);
  *consumed = TRUE;

  if (self->frames_posted >= _batch_frame_bound(self))
    _close_batch(self);

  if (_drain_output(self) == LPS_ERROR)
    return _close_connection(self, LPS_ERROR);

  return LPS_SUCCESS;
}

/* The LogWriter calls this once at the end of every pass, so a Batch is closed
 * as soon as the writer runs out of messages -- the time bound of 8.4, met
 * without a timer of its own.
 *
 * A Batch with no Frame in it is left open on purpose: that is where an idle
 * Sender waits, because the Receiver applies its idle timeout in command state
 * only, so a Sender idling outside a Batch would be closed (ADR-0010).
 */
static LogProtoStatus
log_proto_altp_client_flush(LogProtoClient *s)
{
  LogProtoAltpClient *self = (LogProtoAltpClient *) s;

  if (self->state == ALTP_SENDER_CLOSED)
    return LPS_EOF;

  if (self->state == ALTP_SENDER_IN_BATCH && self->frames_posted > 0)
    _close_batch(self);

  LogProtoStatus status = _drain_output(self);

  if (status == LPS_ERROR)
    return _close_connection(self, status);

  return status;
}

/* The LogWriter uses @cond as it stands when we return TRUE, and picks between
 * @cond and @idle_cond by the state of its queue when we return FALSE.  The
 * timeout it arms CLOSES the connection on expiry, which is precisely what the
 * acknowledgement timeout of specification 9.5 asks for.
 */
static gboolean
log_proto_altp_client_poll_prepare(LogProtoClient *s, GIOCondition *cond, GIOCondition *idle_cond, gint *timeout)
{
  LogProtoAltpClient *self = (LogProtoAltpClient *) s;

  *cond = G_IO_IN;
  *idle_cond = G_IO_IN;
  if (_has_pending_output(self))
    *cond |= G_IO_OUT;

  switch (self->state)
    {
    case ALTP_SENDER_AWAIT_BANNER:
    case ALTP_SENDER_EHLO_SENT:
    case ALTP_SENDER_TLS_REQUESTED:
    case ALTP_SENDER_ZLIB_REQUESTED:
    case ALTP_SENDER_DATA_SENT:
      /* a reply is due, so the queue must not be polled; the timeout is armed
       * here too, or a handshake that never answers would hold the connection
       * -- and every Frame the Session retains -- forever */
      *timeout = self->options.response_timeout;
      return TRUE;

    case ALTP_SENDER_SYNC_SENT:
    case ALTP_SENDER_BATCH_CLOSED:
      /* the acknowledgement timeout proper: on expiry the connection closes
       * with every unacknowledged Frame retained (9.5) */
      *timeout = self->options.ack_timeout;
      return TRUE;

    case ALTP_SENDER_IN_BATCH:
      if (_has_pending_output(self))
        return TRUE;

      /* Frames may be written, so the LogWriter decides by the state of its
       * queue.  No acknowledgement is outstanding, so no timeout of ours is
       * armed; the wrapper substitutes idle-timeout() instead. */
      *cond = G_IO_IN | G_IO_OUT;
      return FALSE;

    case ALTP_SENDER_CLOSED:
      return TRUE;

    default:
      g_assert_not_reached();
    }
}

/* Called by afsocket right after every Connection is constructed.  It is where
 * the Session ID and the frames_sent of the Batch a previous Connection left
 * open come from (10.2), and the only identification a LogProtoClient is
 * given, so it is where our metrics are registered too.
 */
static gboolean
log_proto_altp_client_restart_with_state(LogProtoClient *s, PersistState *state, const gchar *persist_name)
{
  LogProtoAltpClient *self = (LogProtoAltpClient *) s;

  if (!persist_name)
    return FALSE;

  if (!self->metrics.id)
    _register_metrics(self, persist_name);

  if (!state || self->persist_handle)
    return TRUE;

  gchar *entry_name = _format_entry_name(persist_name);
  gsize size = 0;
  guint8 entry_format_version = 0;

  self->persist_state = state;
  self->persist_handle = persist_state_lookup_entry(state, entry_name, &size, &entry_format_version);

  if (self->persist_handle && _load_sender_state(self, self->persist_handle, size, entry_name))
    {
      msg_debug("Resuming an ALTP Session from the persistent state of the destination",
                evt_tag_str("entry", entry_name),
                evt_tag_str("session_id", self->session_id),
                evt_tag_int("frames_sent", self->frames_sent));
    }
  else
    {
      /* Alloc replaces an entry of the same name, which is how a refused one
       * is discarded.  The generated Session ID stands and frames_sent starts
       * at 0: the Receiver answers `250 Received 0` for a Session ID it never
       * saw, so nothing is released that was not sent (10.2). */
      self->frames_sent = 0;
      self->persist_handle = persist_state_alloc_entry(state, entry_name, sizeof(AltpSenderPersistedState));
      if (self->persist_handle)
        {
          _persist_sender_state(self);
          msg_debug("Starting a new ALTP Session for this destination",
                    evt_tag_str("entry", entry_name),
                    evt_tag_str("session_id", self->session_id));
        }
      else
        {
          msg_error("Cannot allocate the persistent state of an ALTP Session, the delivery progress of this "
                    "destination will not survive a restart",
                    evt_tag_str("entry", entry_name));
          self->persist_state = NULL;
        }
    }

  g_free(entry_name);

  return TRUE;
}

static void
log_proto_altp_client_free(LogProtoClient *s)
{
  LogProtoAltpClient *self = (LogProtoAltpClient *) s;

  _unregister_metrics(self);

  /* The Frames of the open Batch stay in the flow control backlog: a Sender
   * retains every Frame until it is acknowledged and the SYNC of the next
   * Connection reconciles them (10.2).  Unlike the text client, nothing is
   * rewound here. */
  g_string_free(self->out_buf, TRUE);
  g_array_free(self->frame_ends, TRUE);

  log_proto_client_free_method(s);
}

LogProtoClient *
log_proto_altp_client_new(LogTransport *transport, const LogProtoClientOptions *options,
                          const AltpSenderOptions *altp_options)
{
  LogProtoAltpClient *self = g_new0(LogProtoAltpClient, 1);

  log_proto_client_init(&self->super, transport, options);
  self->super.poll_prepare = log_proto_altp_client_poll_prepare;
  self->super.post = log_proto_altp_client_post;
  self->super.process_in = log_proto_altp_client_process_in;
  self->super.flush = log_proto_altp_client_flush;
  self->super.restart_with_state = log_proto_altp_client_restart_with_state;
  self->super.free_fn = log_proto_altp_client_free;

  self->options = *altp_options;
  self->state = ALTP_SENDER_AWAIT_BANNER;
  self->out_buf = g_string_sized_new(ALTP_MAX_REPLY_LINE);
  self->frame_ends = g_array_new(FALSE, FALSE, sizeof(gsize));

  /* a destination without a persistent state still needs a Session ID; one
   * with a state has this replaced by the persisted one */
  _generate_session_id(self);

  return &self->super;
}
