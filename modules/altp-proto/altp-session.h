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
#include "logproto/logproto-server.h"

/* A Session ID is 1 to 64 visible ASCII octets (specification 7.1) */
#define ALTP_MAX_SESSION_ID_LENGTH 64

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

/* Look up the Session Record of @session_id, creating one with zero counters
 * when the Session is unknown to us (7.2).  Returns a new reference.
 *
 * Stage B2: this is where a Session Record that is not in memory is loaded
 * from the persistent state of the Receiver before a new one is created.
 */
AltpSessionRecord *altp_session_registry_lookup(AltpSessionRegistry *self, const gchar *session_id);

guint altp_session_registry_get_session_count(AltpSessionRegistry *self);

/****************************************************************************
 * Session Record: the Receiver's state of one Session (specification 10.1).
 ****************************************************************************/

AltpSessionRecord *altp_session_record_ref(AltpSessionRecord *self);
void altp_session_record_unref(AltpSessionRecord *self);

const gchar *altp_session_record_get_session_id(AltpSessionRecord *self);

/* Bind @owner as the Owning Connection of the Session, displacing and waking
 * the Connection that held it before, if any (newest connection wins, 7.3).
 * Returns TRUE when another Connection was displaced. */
gboolean altp_session_record_take_over(AltpSessionRecord *self, AltpSessionOwner *owner);

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
