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

/* The receiver side of the Lumberjack protocol (the Beats protocol), versions
 * 1 and 2, as specified by the Lumberjack protocol specification of
 * https://github.com/axoflow/lumberjack-specs.  Section numbers below refer to
 * that document.
 *
 * A connection is a sequence of windows: a `W` frame announcing N, then N
 * data frames (`J` in version 2, `D` in version 1) numbered 1..N, which the
 * receiver acknowledges with a single `A(N)` once it took responsibility for
 * them (7, 8).  The sequence numbers are not enforced: senders that number
 * from a counter running across windows (logstash-forwarder, ruby-lumberjack,
 * ferro-lumberjack) are acknowledged with the last sequence number they sent,
 * the only value they can correlate, which is N for a conforming sender
 * (8.2, 8.3).  Here every data frame becomes a LogMessage as soon as it is
 * parsed and the window is acknowledged once the pipeline acknowledged its
 * last frame: with flags(flow-control) and a disk-buffer downstream the ACK
 * means "stored", without flow control it degrades to "received", the
 * semantics of the canonical receivers.  While a window waits, `A(0)`
 * keepalives keep the sender from timing out (10).
 *
 * Compressed (`C`) frames are not supported yet and close the connection as a
 * protocol error.  TLS is the transport's business: a tls() block on the
 * driver puts a TLS factory on the stack and this proto switches to it before
 * the first byte is read (15.2).
 */

#include "logproto-lumberjack-server.h"
#include "gsocket.h"
#include "logmsg/logmsg.h"
#include "mainloop.h"
#include "messages.h"
#include "metrics/metric-names.h"
#include "stats/stats-cluster-single.h"
#include "stats/stats-registry.h"
#include "timeutils/misc.h"
#include "utf8utils.h"

#include <iv.h>

#include <errno.h>
#include <string.h>

#define LUMBERJACK_VERSION_1 '1'
#define LUMBERJACK_VERSION_2 '2'

#define LUMBERJACK_TYPE_WINDOW     'W'
#define LUMBERJACK_TYPE_DATA       'D'
#define LUMBERJACK_TYPE_JSON       'J'
#define LUMBERJACK_TYPE_COMPRESSED 'C'
#define LUMBERJACK_TYPE_ACK        'A'

/* version, type, uint32 (4.1) */
#define LUMBERJACK_WINDOW_HEADER_LEN 6
#define LUMBERJACK_ACK_LEN           6
/* version, type */
#define LUMBERJACK_FRAME_TYPE_LEN    2
/* version, type, sequence, payload length or pair count (5.2, 6.2) */
#define LUMBERJACK_DATA_HEADER_LEN   10
#define LUMBERJACK_FIELD_LEN_LEN     4

/* the number of read()s a single fetch() is allowed to issue, to keep one
 * connection from starving the others */
static const guint MAX_FETCH_COUNT = 3;

/* the `reason` label values of lumberjack_protocol_errors_total */
enum
{
  LUMBERJACK_ERR_VERSION,
  LUMBERJACK_ERR_FRAME_TYPE,
  LUMBERJACK_ERR_WINDOW_SIZE,
  LUMBERJACK_ERR_LENGTH,
  LUMBERJACK_ERR_COMPRESSION,
  LUMBERJACK_ERR_MAX,
};

static const gchar *LUMBERJACK_ERROR_REASONS[LUMBERJACK_ERR_MAX] =
{
  "version", "frame_type", "window_size", "length", "compression",
};

/* the `version` label values of lumberjack_frames_total */
enum
{
  LUMBERJACK_V1,
  LUMBERJACK_V2,
  LUMBERJACK_VERSIONS,
};

static const gchar *LUMBERJACK_VERSION_LABELS[LUMBERJACK_VERSIONS] = { "1", "2" };

typedef enum
{
  /* the TLS switch, if tls() was configured, before anything is read */
  LUMBERJACK_START,
  /* between windows: a `W` frame is expected (7.2) */
  LUMBERJACK_WINDOW_HEADER,
  /* inside a window: the version and type of the next data frame */
  LUMBERJACK_FRAME_HEADER,
  /* the payload of a `J` frame is being read */
  LUMBERJACK_JSON_PAYLOAD,
  /* the payload of a frame above log-msg-size() is being discarded */
  LUMBERJACK_SKIP_PAYLOAD,
  /* the key/value pairs of a version 1 `D` frame are being read */
  LUMBERJACK_DATA_PAIRS,
  /* out_buf, acknowledgements or keepalives, is being written */
  LUMBERJACK_SENDING,
  LUMBERJACK_CLOSED,
} LumberjackState;

/* the sub-states of LUMBERJACK_DATA_PAIRS (5.2) */
typedef enum
{
  LUMBERJACK_PAIR_KEY_LEN,
  LUMBERJACK_PAIR_KEY,
  LUMBERJACK_PAIR_VALUE_LEN,
  LUMBERJACK_PAIR_VALUE,
  /* every pair is read, the frame waits to be delivered */
  LUMBERJACK_PAIR_DONE,
} LumberjackPairState;

typedef enum
{
  LUMBERJACK_CTRL_NEXT_STATE,
  LUMBERJACK_CTRL_RETURN_WITH_STATUS,
} LumberjackStepControl;

typedef struct _LumberjackFetchContext
{
  LogMessage **msg;
  LogTransportAuxData *aux;
  Bookmark *bookmark;

  /* this fetch() started in a state that asked for write-only readiness, so it
   * must produce no message: a broken promise trips the window assertion of
   * log_source_post() (see poll_prepare() below) */
  gboolean no_message;
} LumberjackFetchContext;

/* A window whose frames were all read, waiting for the pipeline to
 * acknowledge them.  @end_position is the number of frames delivered on the
 * connection up to and including its last frame: once that many are durable,
 * A(ack_seq) goes out (8.3).  A window whose every frame was dropped has
 * end_position of the frame before it and is acknowledged right away. */
typedef struct _LumberjackPendingWindow
{
  guchar version;
  guint32 size;
  /* the value of the acknowledgement: the last sequence number seen, or the
   * size for a sender numbering from 1 (see _frame_complete()) */
  guint32 ack_seq;
  guint64 end_position;
} LumberjackPendingWindow;

/* Shared between the connection and the Bookmarks of the messages it
 * delivered.  A Bookmark may outlive the connection, the pipeline
 * acknowledges at its own pace, so this is reference counted and the
 * connection detaches its wakeup callback when it goes away.
 *
 * The consecutive ack tracker saves the Bookmark of the last message of every
 * newly durable prefix (consecutive_ack_tracker.c), so @durable is a prefix
 * count of delivered frames and never decreases.
 */
typedef struct _LumberjackAckState
{
  GMutex lock;
  gint ref_cnt;
  guint64 durable;
  /* the connection wants a wakeup once @durable reaches @wanted; 0 means it
   * is not waiting */
  guint64 wanted;
  LogProtoServerWakeupCallback *wakeup;
} LumberjackAckState;

typedef struct _LumberjackBookmarkData
{
  LumberjackAckState *state;
  guint64 position;
} LumberjackBookmarkData;

G_STATIC_ASSERT(sizeof(LumberjackBookmarkData) <= sizeof(BookmarkContainer));

typedef struct _LogProtoLumberjackServer
{
  LogProtoServer super;

  LumberjackReceiverOptions options;
  LumberjackState state;
  /* where SENDING returns to once out_buf has been written */
  LumberjackState next_state;

  /* the window being read (7.2) */
  struct
  {
    guchar version;
    guint32 size;
    /* frames read so far, delivered or dropped */
    guint32 done;
    /* the sequence number of the last frame read */
    guint32 last_seq;
  } window;

  /* the version 2 `J` frame being read (6.2) */
  struct
  {
    /* the declared payload length */
    gsize len;
    /* the octets of an oversize payload still to be discarded */
    gsize skip_remaining;
  } json_frame;

  /* the version 1 `D` frame being read (5.2) */
  struct
  {
    guint32 pairs_remaining;
    LumberjackPairState pair_state;
    /* the declared length of the key or value being read */
    gsize field_len;
    /* the frame outgrew log-msg-size() and is being discarded */
    gboolean dropping;
    /* the JSON object the frame is turned into */
    GString *json;
  } data_frame;

  /* frames delivered upstream on this connection: the position a Bookmark
   * carries and the end_position of a pending window */
  guint64 delivered;
  /* LumberjackPendingWindow, oldest first: acknowledged in order (11) */
  GQueue *pending;
  LumberjackAckState *ack_state;

  /* set by the keepalive timer on the main thread, read by fetch(), which may
   * run on an I/O worker thread */
  gint keepalive_due;
  struct iv_timer keepalive_timer;

  /* the unparsed input is buffer[buffer_pos .. buffer_end) */
  guchar *buffer;
  gsize buffer_size, buffer_pos, buffer_end;
  guint fetch_counter;

  /* the acknowledgements being written, out_buf[out_pos ..) is still unwritten */
  GString *out_buf;
  gsize out_pos;

  /* auxiliary data (peer address, timestamps, ...) of the buffered input */
  LogTransportAuxData buffer_aux;

  NVHandle version_handle;
  struct
  {
    /* a clone of the builder of the driver, NULL in a unit test without one */
    StatsClusterKeyBuilder *kb;

    StatsClusterKey *frames_key[LUMBERJACK_VERSIONS];
    StatsCounterItem *frames[LUMBERJACK_VERSIONS];
    StatsClusterKey *acknowledgements_key;
    StatsCounterItem *acknowledgements;
    StatsClusterKey *dropped_key;
    StatsCounterItem *dropped;
    StatsClusterKey *errors_key[LUMBERJACK_ERR_MAX];
    StatsCounterItem *errors[LUMBERJACK_ERR_MAX];
  } metrics;
} LogProtoLumberjackServer;

/****************************************************************************
 * Helpers
 ****************************************************************************/

static inline guint32
_read_u32(const guchar *p)
{
  return ((guint32) p[0] << 24) | ((guint32) p[1] << 16) | ((guint32) p[2] << 8) | (guint32) p[3];
}

static inline void
_append_u32(GString *s, guint32 v)
{
  guchar buf[4] = { v >> 24, v >> 16, v >> 8, v };

  g_string_append_len(s, (const gchar *) buf, sizeof(buf));
}

static inline gsize
_buffered(LogProtoLumberjackServer *self)
{
  return self->buffer_end - self->buffer_pos;
}

static inline const guchar *
_input(LogProtoLumberjackServer *self)
{
  return &self->buffer[self->buffer_pos];
}

/* The peer for log messages, formatted into @buf: the address the transport
 * put in the auxiliary data of the last read, or failing that the peer of the
 * socket.  Only error and debug paths need it. */
static const gchar *
_format_peer_address(LogProtoLumberjackServer *self, gchar *buf, gsize buf_len)
{
  GSockAddr *from_socket = NULL;
  GSockAddr *peer = self->buffer_aux.peer_addr;

  if (!peer)
    peer = from_socket = g_socket_get_peer_name(self->super.transport_stack.fd);

  if (peer)
    g_sockaddr_format(peer, buf, buf_len, GSA_FULL);
  else
    g_strlcpy(buf, "unknown", buf_len);

  g_sockaddr_unref(from_socket);

  return buf;
}

static gsize
_get_max_frame_size(LogProtoLumberjackServer *self)
{
  return (gsize) self->super.options->max_msg_size;
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

static void
_register_metrics(LogProtoLumberjackServer *self)
{
  StatsClusterKeyBuilder *kb = self->metrics.kb;

  if (!kb)
    return;

  for (gint i = 0; i < LUMBERJACK_VERSIONS; i++)
    self->metrics.frames_key[i] = _build_key(kb, METRIC(lumberjack_frames_total), "version",
                                             LUMBERJACK_VERSION_LABELS[i]);
  self->metrics.acknowledgements_key = _build_key(kb, METRIC(lumberjack_acknowledgements_total), NULL, NULL);
  self->metrics.dropped_key = _build_key(kb, METRIC(lumberjack_frames_dropped_total), "reason", "too_large");
  for (gint i = 0; i < LUMBERJACK_ERR_MAX; i++)
    self->metrics.errors_key[i] = _build_key(kb, METRIC(lumberjack_protocol_errors_total), "reason",
                                             LUMBERJACK_ERROR_REASONS[i]);

  stats_lock();
  for (gint i = 0; i < LUMBERJACK_VERSIONS; i++)
    stats_register_counter(STATS_LEVEL1, self->metrics.frames_key[i], SC_TYPE_SINGLE_VALUE, &self->metrics.frames[i]);
  stats_register_counter(STATS_LEVEL1, self->metrics.acknowledgements_key, SC_TYPE_SINGLE_VALUE,
                         &self->metrics.acknowledgements);
  stats_register_counter(STATS_LEVEL1, self->metrics.dropped_key, SC_TYPE_SINGLE_VALUE, &self->metrics.dropped);
  for (gint i = 0; i < LUMBERJACK_ERR_MAX; i++)
    stats_register_counter(STATS_LEVEL1, self->metrics.errors_key[i], SC_TYPE_SINGLE_VALUE, &self->metrics.errors[i]);
  stats_unlock();
}

static void
_unregister_metrics(LogProtoLumberjackServer *self)
{
  if (!self->metrics.kb)
    return;

  stats_lock();
  for (gint i = 0; i < LUMBERJACK_VERSIONS; i++)
    stats_unregister_counter(self->metrics.frames_key[i], SC_TYPE_SINGLE_VALUE, &self->metrics.frames[i]);
  stats_unregister_counter(self->metrics.acknowledgements_key, SC_TYPE_SINGLE_VALUE, &self->metrics.acknowledgements);
  stats_unregister_counter(self->metrics.dropped_key, SC_TYPE_SINGLE_VALUE, &self->metrics.dropped);
  for (gint i = 0; i < LUMBERJACK_ERR_MAX; i++)
    stats_unregister_counter(self->metrics.errors_key[i], SC_TYPE_SINGLE_VALUE, &self->metrics.errors[i]);
  stats_unlock();

  for (gint i = 0; i < LUMBERJACK_VERSIONS; i++)
    stats_cluster_key_free(self->metrics.frames_key[i]);
  stats_cluster_key_free(self->metrics.acknowledgements_key);
  stats_cluster_key_free(self->metrics.dropped_key);
  for (gint i = 0; i < LUMBERJACK_ERR_MAX; i++)
    stats_cluster_key_free(self->metrics.errors_key[i]);

  stats_cluster_key_builder_free(self->metrics.kb);
  memset(&self->metrics, 0, sizeof(self->metrics));
}

/****************************************************************************
 * Durability: the shared acknowledgement state and the Bookmark of one frame
 ****************************************************************************/

static LumberjackAckState *
_ack_state_new(LogProtoServerWakeupCallback *wakeup)
{
  LumberjackAckState *self = g_new0(LumberjackAckState, 1);

  g_mutex_init(&self->lock);
  self->ref_cnt = 1;
  self->wakeup = wakeup;
  return self;
}

static LumberjackAckState *
_ack_state_ref(LumberjackAckState *self)
{
  g_atomic_int_inc(&self->ref_cnt);
  return self;
}

static void
_ack_state_unref(LumberjackAckState *self)
{
  if (g_atomic_int_dec_and_test(&self->ref_cnt))
    {
      g_mutex_clear(&self->lock);
      g_free(self);
    }
}

static guint64
_ack_state_get_durable(LumberjackAckState *self)
{
  guint64 durable;

  g_mutex_lock(&self->lock);
  durable = self->durable;
  g_mutex_unlock(&self->lock);
  return durable;
}

/* The connection asks to be woken up once @wanted frames are durable, 0 to
 * stop waiting.  Returns TRUE, and arms nothing, if they are durable already:
 * the test and the arming share one critical section because a
 * _bookmark_save() between two separate steps would have found nothing to
 * wake and the wakeup would never have come. */
static gboolean
_ack_state_set_wanted(LumberjackAckState *self, guint64 wanted)
{
  gboolean durable_already;

  g_mutex_lock(&self->lock);
  durable_already = self->durable >= wanted;
  self->wanted = durable_already ? 0 : wanted;
  g_mutex_unlock(&self->lock);
  return durable_already;
}

/* the connection is going away, its Bookmarks may not */
static void
_ack_state_detach(LumberjackAckState *self)
{
  g_mutex_lock(&self->lock);
  self->wakeup = NULL;
  self->wanted = 0;
  g_mutex_unlock(&self->lock);
}

/* Runs on the destination thread, for the last frame of every newly durable
 * prefix (consecutive_ack_tracker.c). */
static void
_bookmark_save(Bookmark *bookmark)
{
  LumberjackBookmarkData *data = (LumberjackBookmarkData *) &bookmark->container;
  LumberjackAckState *state = data->state;

  g_mutex_lock(&state->lock);
  if (data->position > state->durable)
    state->durable = data->position;

  msg_trace("Lumberjack frames became durable",
            evt_tag_long("durable", state->durable),
            evt_tag_long("wanted", state->wanted));

  if (state->wanted && state->durable >= state->wanted && state->wakeup)
    {
      state->wanted = 0;
      log_proto_server_wakeup_cb_call(state->wakeup);
    }
  g_mutex_unlock(&state->lock);
}

static void
_bookmark_destroy(Bookmark *bookmark)
{
  LumberjackBookmarkData *data = (LumberjackBookmarkData *) &bookmark->container;

  _ack_state_unref(data->state);
  data->state = NULL;
  bookmark->save = NULL;
  bookmark->destroy = NULL;
}

static void
_bookmark_fill(Bookmark *bookmark, LumberjackAckState *state, guint64 position)
{
  LumberjackBookmarkData *data = (LumberjackBookmarkData *) &bookmark->container;

  /* the ack tracker hands out the same pending Bookmark again when the fetch()
   * it was requested for produced no message, so an earlier fill of ours may
   * still be in there with a reference of its own */
  if (bookmark->destroy == _bookmark_destroy)
    _bookmark_destroy(bookmark);

  data->state = _ack_state_ref(state);
  data->position = position;
  bookmark->save = _bookmark_save;
  bookmark->destroy = _bookmark_destroy;
}

/****************************************************************************
 * Acknowledgements (specification 8)
 ****************************************************************************/

static LumberjackPendingWindow *
_head_pending(LogProtoLumberjackServer *self)
{
  return (LumberjackPendingWindow *) g_queue_peek_head(self->pending);
}

/* the oldest pending window is durable, its A(N) can go out */
static gboolean
_is_acknowledgement_due(LogProtoLumberjackServer *self)
{
  LumberjackPendingWindow *head = _head_pending(self);

  if (!head)
    return FALSE;
  return _ack_state_get_durable(self->ack_state) >= head->end_position;
}

static gboolean
_is_keepalive_due(LogProtoLumberjackServer *self)
{
  return g_atomic_int_get(&self->keepalive_due) != 0;
}

/* keepalives are a version 2 feature: a version 1 receiver sends none (5.5, 10.4) */
static gboolean
_is_keepalive_applicable(LogProtoLumberjackServer *self)
{
  LumberjackPendingWindow *head = _head_pending(self);

  return self->options.keepalive_interval > 0 && head && head->version == LUMBERJACK_VERSION_2;
}

static void
_append_ack(LogProtoLumberjackServer *self, guchar version, guint32 seq)
{
  g_string_append_c(self->out_buf, version);
  g_string_append_c(self->out_buf, LUMBERJACK_TYPE_ACK);
  _append_u32(self->out_buf, seq);
}

/* Queue every acknowledgement and keepalive that is due and switch to
 * SENDING if anything got queued.  Called at the start of a fetch(), before
 * any input is parsed, so an ACK is never held up by a long window. */
static void
_queue_due_output(LogProtoLumberjackServer *self)
{
  gchar peer[MAX_SOCKADDR_STRING];
  guint64 durable = _ack_state_get_durable(self->ack_state);
  LumberjackPendingWindow *head;

  while ((head = _head_pending(self)) && durable >= head->end_position)
    {
      msg_debug("Acknowledging a Lumberjack window",
                evt_tag_int("window_size", head->size),
                evt_tag_int("sequence", head->ack_seq),
                evt_tag_str("client", _format_peer_address(self, peer, sizeof(peer))),
                evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
      _append_ack(self, head->version, head->ack_seq);
      stats_counter_inc(self->metrics.acknowledgements);
      g_queue_pop_head(self->pending);
      g_free(head);
    }

  if (_is_keepalive_due(self))
    {
      g_atomic_int_set(&self->keepalive_due, 0);
      if (_is_keepalive_applicable(self))
        {
          msg_trace("Sending a Lumberjack keepalive",
                    evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
          _append_ack(self, LUMBERJACK_VERSION_2, 0);
        }
    }

  if (self->out_buf->len > self->out_pos && self->state != LUMBERJACK_SENDING)
    {
      self->next_state = self->state;
      self->state = LUMBERJACK_SENDING;
    }
}

static LumberjackStepControl
_flush_output(LogProtoLumberjackServer *self, LogProtoStatus *status)
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
          msg_error("Error writing Lumberjack acknowledgement",
                    evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd),
                    evt_tag_error(EVT_TAG_OSERROR));
          *status = LPS_ERROR;
          return LUMBERJACK_CTRL_RETURN_WITH_STATUS;
        }

      /* a partial write or EAGAIN: stay in SENDING, wait for writability */
      *status = LPS_SUCCESS;
      return LUMBERJACK_CTRL_RETURN_WITH_STATUS;
    }

  g_string_truncate(self->out_buf, 0);
  self->out_pos = 0;
  self->state = self->next_state;
  return LUMBERJACK_CTRL_NEXT_STATE;
}

/****************************************************************************
 * Protocol errors (specification 14.1)
 ****************************************************************************/

/* every protocol error closes the connection without acknowledging: the
 * sender reconnects and retransmits the window (14.1, 14.3) */
static LumberjackStepControl
_protocol_error(LogProtoLumberjackServer *self, gint reason)
{
  stats_counter_inc(self->metrics.errors[reason]);
  g_string_truncate(self->out_buf, 0);
  self->out_pos = 0;
  self->state = LUMBERJACK_CLOSED;
  return LUMBERJACK_CTRL_NEXT_STATE;
}

/****************************************************************************
 * Input
 ****************************************************************************/

static void
_ensure_buffer(LogProtoLumberjackServer *self)
{
  if (G_LIKELY(self->buffer))
    return;

  self->buffer_size = MAX((gsize) self->super.options->init_buffer_size, (gsize) 64);
  self->buffer = g_malloc(self->buffer_size);
}

static void
_compact_buffer(LogProtoLumberjackServer *self)
{
  if (self->buffer_pos == 0)
    return;

  memmove(self->buffer, &self->buffer[self->buffer_pos], self->buffer_end - self->buffer_pos);
  self->buffer_end -= self->buffer_pos;
  self->buffer_pos = 0;
}

/* Make room for @needed octets at buffer_pos.  A payload we buffer is capped
 * at log-msg-size(), so this never grows the buffer beyond max_msg_size. */
static void
_ensure_buffer_space(LogProtoLumberjackServer *self, gsize needed)
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
_fetch_input(LogProtoLumberjackServer *self, LogProtoStatus *status)
{
  gchar peer[MAX_SOCKADDR_STRING];
  *status = LPS_SUCCESS;

  if (self->fetch_counter++ >= MAX_FETCH_COUNT)
    return FALSE;

  _compact_buffer(self);
  g_assert(self->buffer_end < self->buffer_size);

  log_transport_aux_data_reinit(&self->buffer_aux);
  gssize rc = log_transport_stack_read(&self->super.transport_stack,
                                       &self->buffer[self->buffer_end], self->buffer_size - self->buffer_end,
                                       &self->buffer_aux);
  if (rc < 0)
    {
      if (errno != EAGAIN && errno != EINTR)
        {
          msg_error("Error reading Lumberjack input",
                    evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd),
                    evt_tag_error(EVT_TAG_OSERROR));
          *status = LPS_ERROR;
        }
      return FALSE;
    }

  if (rc == 0)
    {
      if (_buffered(self) > 0 || self->state != LUMBERJACK_WINDOW_HEADER)
        msg_notice("EOF on a Lumberjack connection in the middle of a window, the sender will retransmit it",
                   evt_tag_int("window_size", self->window.size),
                   evt_tag_int("frames_read", self->window.done),
                   evt_tag_str("client", _format_peer_address(self, peer, sizeof(peer))),
                   evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
      else
        msg_trace("EOF occurred while reading Lumberjack input",
                  evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
      *status = LPS_EOF;
      return FALSE;
    }

  self->buffer_end += rc;
  return TRUE;
}

/* the whole frame header is buffered, or enough of it to reject it */
static gboolean
_frame_header_available(LogProtoLumberjackServer *self)
{
  gsize avail = _buffered(self);

  if (avail < LUMBERJACK_FRAME_TYPE_LEN)
    return FALSE;

  const guchar *p = _input(self);
  if (p[0] != self->window.version)
    return TRUE;
  if (p[1] == LUMBERJACK_TYPE_JSON || p[1] == LUMBERJACK_TYPE_DATA)
    return avail >= LUMBERJACK_DATA_HEADER_LEN;
  return TRUE;
}

/* the next step of a version 1 `D` frame can be taken without reading */
static gboolean
_pair_step_available(LogProtoLumberjackServer *self)
{
  gsize avail = _buffered(self);

  switch (self->data_frame.pair_state)
    {
    case LUMBERJACK_PAIR_KEY_LEN:
    case LUMBERJACK_PAIR_VALUE_LEN:
      return avail >= LUMBERJACK_FIELD_LEN_LEN;
    case LUMBERJACK_PAIR_KEY:
    case LUMBERJACK_PAIR_VALUE:
      if (self->data_frame.dropping)
        return self->data_frame.field_len == 0 || avail > 0;
      return avail >= self->data_frame.field_len;
    case LUMBERJACK_PAIR_DONE:
    default:
      return TRUE;
    }
}

/****************************************************************************
 * Delivery
 ****************************************************************************/

static void
_note_oversize_frame(LogProtoLumberjackServer *self, gsize declared)
{
  gchar peer[MAX_SOCKADDR_STRING];
  stats_counter_inc(self->metrics.dropped);

  /* The dropped frame is still counted toward the window's acknowledgement
   * (see _frame_complete()), otherwise the sender would retransmit the same
   * window forever. */
  msg_warning_once("Dropping a message larger than log-msg-size(), increase log-msg-size() to accept it",
                   evt_tag_long("message_size", declared),
                   evt_tag_long("log_msg_size", _get_max_frame_size(self)),
                   evt_tag_str("client", _format_peer_address(self, peer, sizeof(peer))),
                   evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
}

/* one frame of the window was read, delivered or dropped (7.2) */
static void
_frame_complete(LogProtoLumberjackServer *self)
{
  self->window.done++;
  if (self->window.done < self->window.size)
    {
      self->state = LUMBERJACK_FRAME_HEADER;
      return;
    }

  LumberjackPendingWindow *pending = g_new0(LumberjackPendingWindow, 1);

  pending->version = self->window.version;
  pending->size = self->window.size;
  /* The last sequence number seen is the count for a sender numbering from 1
   * and the only value a sender with a running counter correlates (8.3).  A
   * last sequence number of 0 would be read as a keepalive (10.2), so such a
   * window is acknowledged by count, as the canonical receiver does. */
  pending->ack_seq = self->window.last_seq ? self->window.last_seq : self->window.size;
  pending->end_position = self->delivered;
  g_queue_push_tail(self->pending, pending);

  msg_trace("Lumberjack window read completely, waiting for its frames to become durable",
            evt_tag_int("window_size", self->window.size),
            evt_tag_long("end_position", self->delivered),
            evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));

  self->window.size = 0;
  self->window.done = 0;
  self->state = LUMBERJACK_WINDOW_HEADER;
}

/* The payload becomes $MESSAGE as it is, no syslog parsing: a Beats event is a
 * JSON object with no syslog header, so the parser would mangle it (6.2).
 * Timestamps are the time of receipt; $HOST comes from the peer address the
 * LogReader applies. */
static LogMessage *
_new_message(LogProtoLumberjackServer *self, const gchar *payload, gsize len)
{
  LogMessage *msg = log_msg_new_empty();
  gint version = self->window.version == LUMBERJACK_VERSION_1 ? LUMBERJACK_V1 : LUMBERJACK_V2;

  log_msg_set_value(msg, LM_V_MESSAGE, payload, len);
  log_msg_set_value(msg, self->version_handle, LUMBERJACK_VERSION_LABELS[version], 1);
  stats_counter_inc(self->metrics.frames[version]);
  return msg;
}

/* Hand @payload upstream as the message of this fetch(), or report that it
 * cannot be right now: either we promised no message from this fetch(), or the
 * ack tracker has no Bookmark for us, which means the flow-control window is
 * full.  The frame then stays buffered and poll_prepare() asks for an immediate
 * fetch once a message is allowed again. */
static gboolean
_deliver(LogProtoLumberjackServer *self, LumberjackFetchContext *ctx, const gchar *payload, gsize len)
{
  if (ctx->no_message || !ctx->bookmark)
    return FALSE;

  *ctx->msg = _new_message(self, payload, len);
  self->delivered++;
  _bookmark_fill(ctx->bookmark, self->ack_state, self->delivered);

  if (ctx->aux)
    log_transport_aux_data_copy(ctx->aux, &self->buffer_aux);
  return TRUE;
}

/****************************************************************************
 * The state machine
 ****************************************************************************/

/* A tls() block on the driver puts a TLS factory on the stack; Lumberjack
 * runs TLS from the very first byte, no STARTTLS, so it is switched to before
 * anything is read.  The handshake happens inside the reads that follow. */
static LumberjackStepControl
_on_start(LogProtoLumberjackServer *self, LogProtoStatus *status)
{
  LogTransportStack *stack = &self->super.transport_stack;

  if (stack->transport_factories[LOG_TRANSPORT_TLS] && stack->active_transport != LOG_TRANSPORT_TLS)
    {
      if (!log_transport_stack_switch(stack, LOG_TRANSPORT_TLS))
        {
          msg_error("Error switching the Lumberjack connection to TLS",
                    evt_tag_int(EVT_TAG_FD, stack->fd));
          *status = LPS_ERROR;
          return LUMBERJACK_CTRL_RETURN_WITH_STATUS;
        }
      msg_debug("Lumberjack connection switched to TLS", evt_tag_int(EVT_TAG_FD, stack->fd));
    }

  self->state = LUMBERJACK_WINDOW_HEADER;
  *status = LPS_SUCCESS;
  return LUMBERJACK_CTRL_RETURN_WITH_STATUS;
}

static LumberjackStepControl
_on_window_header(LogProtoLumberjackServer *self, LogProtoStatus *status)
{
  gchar peer[MAX_SOCKADDR_STRING];
  if (_buffered(self) < LUMBERJACK_WINDOW_HEADER_LEN)
    {
      if (!_fetch_input(self, status))
        return LUMBERJACK_CTRL_RETURN_WITH_STATUS;
      return LUMBERJACK_CTRL_NEXT_STATE;
    }

  const guchar *p = _input(self);
  guchar version = p[0], type = p[1];
  guint32 size = _read_u32(p + 2);

  if (version != LUMBERJACK_VERSION_1 && version != LUMBERJACK_VERSION_2)
    {
      msg_error("Invalid Lumberjack protocol version byte, expected '1' or '2'",
                evt_tag_mem("input", p, LUMBERJACK_WINDOW_HEADER_LEN),
                evt_tag_str("hint", version == 0x16 ? "this looks like a TLS ClientHello, does the client "
                  "expect TLS on a plaintext source?" : ""),
                evt_tag_str("client", _format_peer_address(self, peer, sizeof(peer))),
                evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
      return _protocol_error(self, LUMBERJACK_ERR_VERSION);
    }

  if (type != LUMBERJACK_TYPE_WINDOW)
    {
      msg_error("Unexpected Lumberjack frame, a window frame was expected",
                evt_tag_printf("type", "%c", g_ascii_isprint(type) ? type : '?'),
                evt_tag_mem("input", p, LUMBERJACK_WINDOW_HEADER_LEN),
                evt_tag_str("client", _format_peer_address(self, peer, sizeof(peer))),
                evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
      return _protocol_error(self, LUMBERJACK_ERR_FRAME_TYPE);
    }

  self->buffer_pos += LUMBERJACK_WINDOW_HEADER_LEN;

  if (size == 0)
    {
      /* a window of size 0 announces nothing and needs no acknowledgement (7.3) */
      msg_trace("Ignoring a Lumberjack window frame of size 0",
                evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
      return LUMBERJACK_CTRL_NEXT_STATE;
    }

  if (self->options.max_window_size > 0 && size > (guint32) self->options.max_window_size)
    {
      /* the sender has no way to learn of the limit in-band, so the value it
       * sent is logged for the operator to align max-window-size() with (16) */
      msg_error("Lumberjack window size exceeds max-window-size(), closing the connection; a sender configured "
                "above the receiver's limit retransmits forever, align max-window-size() with its batch size",
                evt_tag_int("window_size", size),
                evt_tag_int("max_window_size", self->options.max_window_size),
                evt_tag_str("client", _format_peer_address(self, peer, sizeof(peer))),
                evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
      return _protocol_error(self, LUMBERJACK_ERR_WINDOW_SIZE);
    }

  self->window.version = version;
  self->window.size = size;
  self->window.done = 0;
  self->window.last_seq = 0;
  self->state = LUMBERJACK_FRAME_HEADER;
  return LUMBERJACK_CTRL_NEXT_STATE;
}

static LumberjackStepControl
_on_frame_header(LogProtoLumberjackServer *self, LogProtoStatus *status)
{
  gchar peer[MAX_SOCKADDR_STRING];
  if (!_frame_header_available(self))
    {
      if (!_fetch_input(self, status))
        return LUMBERJACK_CTRL_RETURN_WITH_STATUS;
      return LUMBERJACK_CTRL_NEXT_STATE;
    }

  const guchar *p = _input(self);
  guchar version = p[0], type = p[1];

  if (version != self->window.version)
    {
      msg_error("Lumberjack frame version differs from the version of its window",
                evt_tag_printf("frame_version", "%c", g_ascii_isprint(version) ? version : '?'),
                evt_tag_printf("window_version", "%c", self->window.version),
                evt_tag_int("sequence", self->window.done + 1),
                evt_tag_str("client", _format_peer_address(self, peer, sizeof(peer))),
                evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
      return _protocol_error(self, LUMBERJACK_ERR_VERSION);
    }

  switch (type)
    {
    case LUMBERJACK_TYPE_JSON:
    case LUMBERJACK_TYPE_DATA:
      break;

    case LUMBERJACK_TYPE_COMPRESSED:
      msg_error("Compressed Lumberjack frames are not supported yet, closing the connection; "
                "disable compression on the sender",
                evt_tag_str("client", _format_peer_address(self, peer, sizeof(peer))),
                evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
      return _protocol_error(self, LUMBERJACK_ERR_COMPRESSION);

    case LUMBERJACK_TYPE_WINDOW:
      msg_error("Lumberjack window frame inside a window, the previous window is incomplete",
                evt_tag_int("window_size", self->window.size),
                evt_tag_int("frames_read", self->window.done),
                evt_tag_str("client", _format_peer_address(self, peer, sizeof(peer))),
                evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
      return _protocol_error(self, LUMBERJACK_ERR_FRAME_TYPE);

    default:
      msg_error("Unknown Lumberjack frame type inside a window",
                evt_tag_printf("type", "%c", g_ascii_isprint(type) ? type : '?'),
                evt_tag_mem("input", p, MIN(_buffered(self), (gsize) 16)),
                evt_tag_str("client", _format_peer_address(self, peer, sizeof(peer))),
                evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
      return _protocol_error(self, LUMBERJACK_ERR_FRAME_TYPE);
    }

  if (type == LUMBERJACK_TYPE_DATA && version != LUMBERJACK_VERSION_1)
    {
      /* a `D` frame with a version byte of '2' is not part of version 2 (6.5, 6.6) */
      msg_error("Lumberjack data frame ('D') in a version 2 window, version 2 carries JSON frames ('J') only",
                evt_tag_str("client", _format_peer_address(self, peer, sizeof(peer))),
                evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
      return _protocol_error(self, LUMBERJACK_ERR_FRAME_TYPE);
    }

  guint32 seq = _read_u32(p + 2);
  guint32 declared = _read_u32(p + 6);

  if (seq != self->window.done + 1)
    {
      /* Not an error (8.3): the frame is counted by position like the
       * canonical receiver does, and the window is acknowledged with the last
       * sequence number seen, the value a running-counter sender expects. */
      msg_trace("Lumberjack frame out of sequence, frames are numbered 1..N within a window by conforming senders",
                evt_tag_int("sequence", seq),
                evt_tag_int("expected", self->window.done + 1),
                evt_tag_int("window_size", self->window.size),
                evt_tag_str("client", _format_peer_address(self, peer, sizeof(peer))),
                evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
    }
  self->window.last_seq = seq;

  self->buffer_pos += LUMBERJACK_DATA_HEADER_LEN;

  if (type == LUMBERJACK_TYPE_JSON)
    {
      if (declared == 0)
        {
          msg_error("Lumberjack JSON frame with an empty payload",
                    evt_tag_str("client", _format_peer_address(self, peer, sizeof(peer))),
                    evt_tag_int(EVT_TAG_FD, self->super.transport_stack.fd));
          return _protocol_error(self, LUMBERJACK_ERR_LENGTH);
        }

      if (declared > _get_max_frame_size(self))
        {
          _note_oversize_frame(self, declared);
          self->json_frame.skip_remaining = declared;
          self->state = LUMBERJACK_SKIP_PAYLOAD;
          return LUMBERJACK_CTRL_NEXT_STATE;
        }

      self->json_frame.len = declared;
      self->state = LUMBERJACK_JSON_PAYLOAD;
      return LUMBERJACK_CTRL_NEXT_STATE;
    }

  self->data_frame.pairs_remaining = declared;
  self->data_frame.pair_state = declared ? LUMBERJACK_PAIR_KEY_LEN : LUMBERJACK_PAIR_DONE;
  self->data_frame.dropping = FALSE;
  g_string_assign(self->data_frame.json, declared ? "{" : "{}");
  self->state = LUMBERJACK_DATA_PAIRS;
  return LUMBERJACK_CTRL_NEXT_STATE;
}

static LumberjackStepControl
_on_json_payload(LogProtoLumberjackServer *self, LumberjackFetchContext *ctx, LogProtoStatus *status)
{
  _ensure_buffer_space(self, self->json_frame.len);

  if (_buffered(self) < self->json_frame.len)
    {
      if (!_fetch_input(self, status))
        return LUMBERJACK_CTRL_RETURN_WITH_STATUS;
      return LUMBERJACK_CTRL_NEXT_STATE;
    }

  if (!_deliver(self, ctx, (const gchar *) _input(self), self->json_frame.len))
    {
      *status = LPS_SUCCESS;
      return LUMBERJACK_CTRL_RETURN_WITH_STATUS;
    }

  self->buffer_pos += self->json_frame.len;
  self->json_frame.len = 0;
  _frame_complete(self);

  *status = LPS_SUCCESS;
  return LUMBERJACK_CTRL_RETURN_WITH_STATUS;
}

/* discard as much of the oversize payload as is buffered, no allocation */
static LumberjackStepControl
_on_skip_payload(LogProtoLumberjackServer *self, LogProtoStatus *status)
{
  gsize skipped = MIN(_buffered(self), self->json_frame.skip_remaining);

  self->buffer_pos += skipped;
  self->json_frame.skip_remaining -= skipped;

  if (self->json_frame.skip_remaining > 0)
    {
      if (!_fetch_input(self, status))
        return LUMBERJACK_CTRL_RETURN_WITH_STATUS;
      return LUMBERJACK_CTRL_NEXT_STATE;
    }

  _frame_complete(self);
  return LUMBERJACK_CTRL_NEXT_STATE;
}

/* append @len octets as a JSON string: quote, backslash and control
 * characters escaped, invalid UTF-8 as a visible \xNN so that the object
 * stays valid UTF-8 */
static void
_append_json_string(GString *json, const guchar *s, gsize len)
{
  g_string_append_c(json, '"');
  append_unsafe_utf8_as_escaped(json, (const gchar *) s, len, AUTF8_UNSAFE_QUOTE, "\\u%04x", "\\\\x%02x");
  g_string_append_c(json, '"');
}

/* A version 1 `D` frame is a count of key/value string pairs (5.2), turned
 * into a flat JSON object so that $MESSAGE is JSON in either version.  Values
 * stay strings and duplicate keys are kept in order; any smarter conversion
 * belongs in a parser downstream.  The pairs are read one at a time, and a
 * frame that outgrows log-msg-size() is discarded pair by pair without being
 * buffered whole. */
static LumberjackStepControl
_on_data_pairs(LogProtoLumberjackServer *self, LumberjackFetchContext *ctx, LogProtoStatus *status)
{
  gsize max = _get_max_frame_size(self);

  while (self->data_frame.pair_state != LUMBERJACK_PAIR_DONE)
    {
      if (!_pair_step_available(self))
        {
          if (!_fetch_input(self, status))
            return LUMBERJACK_CTRL_RETURN_WITH_STATUS;
          continue;
        }

      switch (self->data_frame.pair_state)
        {
        case LUMBERJACK_PAIR_KEY_LEN:
        case LUMBERJACK_PAIR_VALUE_LEN:
          self->data_frame.field_len = _read_u32(_input(self));
          self->buffer_pos += LUMBERJACK_FIELD_LEN_LEN;
          /* the JSON needs the field, its quotes, a colon or comma and the
           * closing brace on top of what is there already */
          gsize needed = self->data_frame.json->len + self->data_frame.field_len;
          if (!self->data_frame.dropping && (self->data_frame.field_len > max || needed + 4 > max))
            {
              _note_oversize_frame(self, needed);
              self->data_frame.dropping = TRUE;
            }
          self->data_frame.pair_state++;
          break;

        case LUMBERJACK_PAIR_KEY:
        case LUMBERJACK_PAIR_VALUE:
          if (self->data_frame.dropping)
            {
              gsize skipped = MIN(_buffered(self), self->data_frame.field_len);

              self->buffer_pos += skipped;
              self->data_frame.field_len -= skipped;
              if (self->data_frame.field_len > 0)
                break;
            }
          else
            {
              if (self->data_frame.pair_state == LUMBERJACK_PAIR_KEY)
                {
                  if (self->data_frame.json->len > 1)
                    g_string_append_c(self->data_frame.json, ',');
                }
              else
                g_string_append_c(self->data_frame.json, ':');
              _append_json_string(self->data_frame.json, _input(self), self->data_frame.field_len);
              self->buffer_pos += self->data_frame.field_len;
              self->data_frame.field_len = 0;
            }

          if (self->data_frame.pair_state == LUMBERJACK_PAIR_KEY)
            self->data_frame.pair_state = LUMBERJACK_PAIR_VALUE_LEN;
          else
            {
              self->data_frame.pairs_remaining--;
              if (self->data_frame.pairs_remaining == 0)
                {
                  g_string_append_c(self->data_frame.json, '}');
                  self->data_frame.pair_state = LUMBERJACK_PAIR_DONE;
                }
              else
                self->data_frame.pair_state = LUMBERJACK_PAIR_KEY_LEN;
            }
          break;

        default:
          g_assert_not_reached();
        }
    }

  if (self->data_frame.dropping)
    {
      _frame_complete(self);
      return LUMBERJACK_CTRL_NEXT_STATE;
    }

  if (!_deliver(self, ctx, self->data_frame.json->str, self->data_frame.json->len))
    {
      *status = LPS_SUCCESS;
      return LUMBERJACK_CTRL_RETURN_WITH_STATUS;
    }

  _frame_complete(self);
  *status = LPS_SUCCESS;
  return LUMBERJACK_CTRL_RETURN_WITH_STATUS;
}

static LumberjackStepControl
_on_closed(LogProtoLumberjackServer *self, LogProtoStatus *status)
{
  /* terminal: report the end of input so that the LogReader closes us */
  *status = LPS_EOF;
  return LUMBERJACK_CTRL_RETURN_WITH_STATUS;
}

static LumberjackStepControl
_step_state_machine(LogProtoLumberjackServer *self, LumberjackFetchContext *ctx, LogProtoStatus *status)
{
  switch (self->state)
    {
    case LUMBERJACK_START:
      return _on_start(self, status);
    case LUMBERJACK_WINDOW_HEADER:
      return _on_window_header(self, status);
    case LUMBERJACK_FRAME_HEADER:
      return _on_frame_header(self, status);
    case LUMBERJACK_JSON_PAYLOAD:
      return _on_json_payload(self, ctx, status);
    case LUMBERJACK_SKIP_PAYLOAD:
      return _on_skip_payload(self, status);
    case LUMBERJACK_DATA_PAIRS:
      return _on_data_pairs(self, ctx, status);
    case LUMBERJACK_SENDING:
      return _flush_output(self, status);
    case LUMBERJACK_CLOSED:
      return _on_closed(self, status);
    default:
      g_assert_not_reached();
    }
}

static LogProtoStatus
log_proto_lumberjack_server_fetch_structured(LogProtoServer *s, LogMessage **msg, LogTransportAuxData *aux,
                                             Bookmark *bookmark)
{
  LogProtoLumberjackServer *self = (LogProtoLumberjackServer *) s;
  LogProtoStatus status = LPS_SUCCESS;
  LumberjackFetchContext ctx =
  {
    .msg = msg,
    .aux = aux,
    .bookmark = bookmark,
    /* every one of these had poll_prepare() ask for write-only readiness */
    .no_message = self->state == LUMBERJACK_START
    || self->state == LUMBERJACK_SENDING
    || self->state == LUMBERJACK_CLOSED
    || _is_acknowledgement_due(self)
    || _is_keepalive_due(self),
  };

  *msg = NULL;
  _ensure_buffer(self);
  self->fetch_counter = 0;

  if (self->state != LUMBERJACK_START && self->state != LUMBERJACK_CLOSED)
    _queue_due_output(self);

  while (_step_state_machine(self, &ctx, &status) != LUMBERJACK_CTRL_RETURN_WITH_STATUS)
    ;

  return status;
}

/****************************************************************************
 * Keepalive (specification 10)
 ****************************************************************************/

/* the fetch() that follows the wakeup writes the A(0) */
static void
_keepalive_timer_expired(gpointer cookie)
{
  LogProtoLumberjackServer *self = (LogProtoLumberjackServer *) cookie;

  g_atomic_int_set(&self->keepalive_due, 1);
  log_proto_server_wakeup_cb_call(&self->super.wakeup_callback);
}

void
log_proto_lumberjack_server_fire_keepalive(LogProtoServer *s)
{
  LogProtoLumberjackServer *self = (LogProtoLumberjackServer *) s;

  if (iv_timer_registered(&self->keepalive_timer))
    iv_timer_unregister(&self->keepalive_timer);
  _keepalive_timer_expired(self);
}

static void
_update_keepalive_timer(LogProtoLumberjackServer *self, gboolean want_armed)
{
  /* an iv_timer belongs to the thread that registered it, while fetch() may
   * run on an I/O worker */
  main_loop_assert_main_thread();

  if (want_armed == !!iv_timer_registered(&self->keepalive_timer))
    return;

  if (!want_armed)
    {
      iv_timer_unregister(&self->keepalive_timer);
      return;
    }

  iv_validate_now();
  self->keepalive_timer.expires = iv_now;
  timespec_add_msec(&self->keepalive_timer.expires, (gint64) self->options.keepalive_interval * 1000);
  iv_timer_register(&self->keepalive_timer);
}

/****************************************************************************
 * poll_prepare
 ****************************************************************************/

static LogProtoPrepareAction
log_proto_lumberjack_server_poll_prepare(LogProtoServer *s, GIOCondition *cond, gint *timeout)
{
  LogProtoLumberjackServer *self = (LogProtoLumberjackServer *) s;
  GIOCondition proto_cond = G_IO_IN;
  gboolean fetch_now = FALSE;

  /* 0 keeps log_proto_server_poll_prepare() from substituting idle-timeout();
   * it is asked for explicitly between windows below */
  *timeout = 0;

  gboolean closed = self->state == LUMBERJACK_CLOSED;
  LumberjackPendingWindow *head = _head_pending(self);
  gboolean output_due = !closed && _is_keepalive_due(self);

  /* wake us up once the oldest pending window is durable, or learn in the
   * same step that it is already and write its acknowledgement now */
  if (!closed && head && !output_due)
    output_due = _ack_state_set_wanted(self->ack_state, head->end_position);
  else
    _ack_state_set_wanted(self->ack_state, 0);

  _update_keepalive_timer(self, !closed && head && !output_due && _is_keepalive_applicable(self));

  if (output_due)
    {
      /* The acknowledgement has to be written even though the frames it
       * acknowledges still occupy the flow-control window: polling for
       * writability is what the LogReader honours with an exhausted window
       * (an immediate fetch it would turn into a suspend), and in exchange
       * fetch() returns no message. */
      proto_cond = G_IO_OUT;
    }
  else
    {
      switch (self->state)
        {
        case LUMBERJACK_START:
        case LUMBERJACK_SENDING:
        case LUMBERJACK_CLOSED:
          /* write only: honoured even with an exhausted flow-control window */
          proto_cond = G_IO_OUT;
          break;

        case LUMBERJACK_WINDOW_HEADER:
          /* waiting for the next window has no timeout of its own (7.4, 13): the
           * idle-timeout() of the driver applies, -1 asks for it */
          *timeout = -1;
          fetch_now = _buffered(self) >= LUMBERJACK_WINDOW_HEADER_LEN;
          break;

        case LUMBERJACK_FRAME_HEADER:
          *timeout = self->options.window_timeout;
          fetch_now = _frame_header_available(self);
          break;

        case LUMBERJACK_JSON_PAYLOAD:
          *timeout = self->options.window_timeout;
          fetch_now = _buffered(self) >= self->json_frame.len;
          break;

        case LUMBERJACK_SKIP_PAYLOAD:
          *timeout = self->options.window_timeout;
          fetch_now = _buffered(self) > 0;
          break;

        case LUMBERJACK_DATA_PAIRS:
          *timeout = self->options.window_timeout;
          fetch_now = _pair_step_available(self);
          break;

        default:
          g_assert_not_reached();
        }
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

static void
log_proto_lumberjack_server_free(LogProtoServer *s)
{
  LogProtoLumberjackServer *self = (LogProtoLumberjackServer *) s;

  if (iv_timer_registered(&self->keepalive_timer))
    iv_timer_unregister(&self->keepalive_timer);

  _unregister_metrics(self);

  /* Bookmarks of messages still in the pipeline keep the state alive, but
   * they must not wake a connection that is gone */
  _ack_state_detach(self->ack_state);
  _ack_state_unref(self->ack_state);

  g_queue_free_full(self->pending, g_free);
  g_free(self->buffer);
  g_string_free(self->out_buf, TRUE);
  g_string_free(self->data_frame.json, TRUE);
  log_transport_aux_data_destroy(&self->buffer_aux);

  log_proto_server_free_method(s);
}

LogProtoServer *
log_proto_lumberjack_server_new(LogTransport *transport, const LogProtoServerOptions *options,
                                const LumberjackReceiverOptions *lumberjack_options, StatsClusterKeyBuilder *kb)
{
  LogProtoLumberjackServer *self = g_new0(LogProtoLumberjackServer, 1);

  log_proto_server_init(&self->super, transport, options);
  self->super.poll_prepare = log_proto_lumberjack_server_poll_prepare;
  self->super.fetch_structured = log_proto_lumberjack_server_fetch_structured;
  self->super.free_fn = log_proto_lumberjack_server_free;

  self->options = *lumberjack_options;
  self->state = LUMBERJACK_START;
  self->out_buf = g_string_sized_new(LUMBERJACK_ACK_LEN * 4);
  self->data_frame.json = g_string_sized_new(256);
  self->pending = g_queue_new();
  self->ack_state = _ack_state_new(&self->super.wakeup_callback);
  self->version_handle = log_msg_get_value_handle(".lumberjack.version");

  IV_TIMER_INIT(&self->keepalive_timer);
  self->keepalive_timer.cookie = self;
  self->keepalive_timer.handler = _keepalive_timer_expired;

  /* the builder is owned by our caller, so a clone is what survives */
  if (kb)
    self->metrics.kb = stats_cluster_key_builder_clone(kb);
  _register_metrics(self);

  return &self->super;
}
