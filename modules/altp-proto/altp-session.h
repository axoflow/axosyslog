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

#ifndef ALTP_SESSION_H_INCLUDED
#define ALTP_SESSION_H_INCLUDED

#include "ack-tracker/bookmark.h"
#include "atomic-gssize.h"
#include "gsockaddr.h"
#include "logproto/logproto-server.h"
#include "persist-state.h"

/* A Session ID is 1 to 64 visible ASCII octets (specification 7.1) */
#define ALTP_MAX_SESSION_ID_LENGTH 64

/* The layout version of a persisted Session Record.  An entry carrying
 * anything else is refused and the Session starts over, which 10.1 allows for
 * a Session whose state was lost.
 */
#define ALTP_SESSION_PERSIST_VERSION 1

/* How often the Session Registry sweeps its Session Records for the ones
 * unused for session_expiration seconds. */
#define ALTP_SESSION_EXPIRY_SWEEP_INTERVAL 3600

/* How often a Receiver at its max-sessions() bound logs a refusal: the peer
 * provoking it is unauthenticated and can do so as fast as it can connect. */
#define ALTP_SESSION_REFUSAL_LOG_INTERVAL 60

typedef struct _AltpSessionRecord AltpSessionRecord;
typedef struct _AltpSessionRegistry AltpSessionRegistry;

/* The Owning Connection's side of a Session Record, embedded in its
 * LogProtoServer.  Every field is read and written under the mutex of the
 * Session Record, so use the altp_session_record_*() methods below rather than
 * reaching in.
 */
typedef struct _AltpSessionOwner
{
  /* how this Connection is woken; called from any thread, which
   * log_reader_wakeup() is prepared for */
  LogProtoServerWakeupCallback *wakeup;

  /* The peer of this Connection, so that a take-over can be logged with both
   * sides named (15).  It lives here and not in the LogProtoServer, because
   * this is the only part of a Connection the other one may read.
   */
  gchar peer_address[MAX_SOCKADDR_STRING];

  /* the Connection is in AWAITING_DURABILITY, waiting for the Batch (12.1) */
  gboolean awaiting;

  /* a newer Connection of the same Session took the Session over, so this one
   * has to close without acknowledging anything (7.3) */
  gboolean displaced;
} AltpSessionOwner;

/****************************************************************************
 * Session Registry: the map from Session ID to Session Record of one
 * Receiver.  A module global table keys it by the persistent name of the
 * driver (ADR-0005), so that a configuration reload -- which recreates the
 * driver but keeps its Connections alive -- finds the very same registry.
 ****************************************************************************/

/* creates the registry of @name when this is the first Receiver to ask */
AltpSessionRegistry *altp_session_registry_ref_by_name(const gchar *name);
AltpSessionRegistry *altp_session_registry_ref(AltpSessionRegistry *self);
void altp_session_registry_unref(AltpSessionRegistry *self);

const gchar *altp_session_registry_get_name(AltpSessionRegistry *self);

/* Bind the registry to the persistent state of the configuration: from here on
 * every Session Record is loaded from and written to the entry
 * "<registry name>.altp.session(<session-id>)".  Binding applies the restart
 * rule of 10.1 -- `frames_read := frames_acked` -- expires the entries unused
 * for @session_expiration seconds and arms the periodic expiry sweep.
 *
 * The first caller wins: a configuration reload binds again and finds the
 * registry bound already, so the Session Records of the Connections that
 * survived stay authoritative.  @session_expiration and @max_sessions (0 for
 * unlimited) are per driver while the registry is per persistent name, so the
 * smaller of two differing values applies.
 */
void altp_session_registry_bind_persist_state(AltpSessionRegistry *self, PersistState *state,
                                              gint session_expiration, gint max_sessions);

/* Look up the Session Record of @session_id: the one in memory, else the one
 * loaded from the persistent state, else a fresh one with zero counters (7.2,
 * 10.1).  Returns a new reference, or NULL when the Session has none yet and
 * the registry is at its max_sessions bound -- a Session that does have one is
 * served at the bound as well, an established one is never evicted.
 */
AltpSessionRecord *altp_session_registry_lookup(AltpSessionRegistry *self, const gchar *session_id);

/* TRUE when a refusal is to be logged now, at most once per Receiver per
 * ALTP_SESSION_REFUSAL_LOG_INTERVAL seconds.
 */
gboolean altp_session_registry_should_log_refusal(AltpSessionRegistry *self);

guint altp_session_registry_get_session_count(AltpSessionRegistry *self);

/* The bound in effect, which is the smallest max-sessions() of the Receivers
 * that bound the registry and not necessarily the one of the caller. */
gint altp_session_registry_get_max_sessions(AltpSessionRegistry *self);

/* The number of Session Records, as stats_register_external_counter() reads a
 * gauge it does not own.  The registry outlives every Connection using it. */
atomic_gssize *altp_session_registry_get_session_count_ref(AltpSessionRegistry *self);

/* test only: run the periodic expiry sweep right now */
void altp_session_registry_fire_expiry(AltpSessionRegistry *self);

/****************************************************************************
 * Session Record: the Receiver's state of one Session (specification 10.1).
 ****************************************************************************/

AltpSessionRecord *altp_session_record_ref(AltpSessionRecord *self);
void altp_session_record_unref(AltpSessionRecord *self);

const gchar *altp_session_record_get_session_id(AltpSessionRecord *self);

/* Bind @owner as the Owning Connection, displacing and waking the Connection
 * that held it before (newest connection wins, 7.3).  Returns TRUE when
 * another Connection was displaced, and then copies its peer address into
 * @displaced_peer_address.  @peer_address is the peer of @owner itself.
 */
gboolean altp_session_record_take_over(AltpSessionRecord *self, AltpSessionOwner *owner, const gchar *peer_address,
                                       gchar *displaced_peer_address, gsize displaced_peer_address_len);

/* The peer address of the Connection that owns the Session now.  FALSE when no
 * Connection is bound to it. */
gboolean altp_session_record_get_owner_peer_address(AltpSessionRecord *self, gchar *peer_address,
                                                    gsize peer_address_len);

/* Give up the ownership if we still hold it; the counters stand (14.2). */
void altp_session_record_release(AltpSessionRecord *self, AltpSessionOwner *owner);

gboolean altp_session_record_is_displaced(AltpSessionRecord *self, AltpSessionOwner *owner);

/* TRUE when every Frame read is durable, i.e. an acknowledgement is due */
gboolean altp_session_record_is_batch_durable(AltpSessionRecord *self);

/* Enter AWAITING_DURABILITY.  When the Batch is durable already it fills
 * @frames_acked and returns TRUE, so the caller acknowledges right away;
 * otherwise it arms the wakeup of @owner and returns FALSE. */
gboolean altp_session_record_wait_for_durability(AltpSessionRecord *self, AltpSessionOwner *owner,
                                                 guint32 *frames_acked);

/* Leave AWAITING_DURABILITY with an acknowledgement of the durable prefix and
 * return the count to report.  @partial disowns the Frames beyond it with
 * `frames_read := frames_acked` (9.3, 12.1).  The counters are otherwise left
 * as they stand, see _process_command_line() in logproto-altp-server.c. */
guint32 altp_session_record_acknowledge(AltpSessionRecord *self, AltpSessionOwner *owner, gboolean partial);

/* The deferred counter reset of ADR-0004, see _process_command_line() in
 * logproto-altp-server.c. */
void altp_session_record_reset_counters(AltpSessionRecord *self);

/* `frames_read += 1` (10.1); @batch_seq and @frame_index identify the Frame
 * for its Bookmark. */
void altp_session_record_note_frame_read(AltpSessionRecord *self, guint64 *batch_seq, guint32 *frame_index);

void altp_session_record_get_counters(AltpSessionRecord *self, guint32 *frames_read, guint32 *frames_acked);

/****************************************************************************
 * Durability: the Bookmark travelling with every Frame delivered upstream.
 ****************************************************************************/

/* Fill @bookmark with a reference to @record and the position of the Frame
 * within its Batch, so that the report of the consecutive ack tracker -- which
 * names the last Frame of every newly durable prefix -- can be turned into the
 * absolute prefix count frames_acked is (10.1).  Filling a Bookmark a previous
 * fetch() filled but did not use is allowed and drops the unused reference.
 */
void altp_session_bookmark_fill(Bookmark *bookmark, AltpSessionRecord *record, guint64 batch_seq,
                                guint32 frame_index);

/* test only: proves that an unused Bookmark leaks no Session Record reference */
guint altp_session_record_get_ref_count(AltpSessionRecord *self);

#endif
