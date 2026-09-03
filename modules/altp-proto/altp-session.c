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

#include "altp-session.h"
#include "messages.h"
#include "timeutils/cache.h"

#include <string.h>

struct _AltpSessionRecord
{
  gint ref_cnt;

  /* the Session ID is the lookup key and is never interpreted (7.1) */
  gchar *session_id;

  /* protects everything below, including every field of @owner: durability
   * reports, Frame reads and the timers all run on different threads */
  GMutex mutex;

  /* the two counters of the current Batch (10.1) */
  guint32 frames_read;
  guint32 frames_acked;

  /* Bumped on every counter reset, so that a durability report of a Batch for
   * which an acknowledgement was already sent -- including a Frame disowned by
   * a partial acknowledgement -- is recognised as stale and ignored (10.1). */
  guint64 batch_seq;

  /* the Owning Connection, NULL while no Connection is bound to the Session */
  AltpSessionOwner *owner;

  gint64 last_used;

  /* Stage B2: PersistState *persist_state and PersistEntryHandle handle of the
   * "<driver persist name>.altp.session(<session-id>)" entry, 0 while the
   * Session Record is memory only.  Every counter update below then persists
   * the record, and altp_session_registry_lookup() loads it. */
};

struct _AltpSessionRegistry
{
  gint ref_cnt;

  /* the persistent name of the Receiver driver: what scopes us (ADR-0005) */
  gchar *name;

  GMutex mutex;
  /* Session ID -> AltpSessionRecord *, one reference held by the table.  The
   * table is what keeps a Session Record alive between two Connections of the
   * Session, so the reference is dropped only when the registry itself goes
   * away.
   *
   * NOTE: a Session Record deliberately holds no reference back to the
   * registry.  The table owns the records, so a reference in the other
   * direction would be a cycle neither side could break.
   */
  GHashTable *sessions;

  /* Stage B2: the PersistState of the configuration, the prefix sweep applying
   * the `frames_read := frames_acked` restart rule of 10.1, and the periodic
   * expiry of records unused for session_expiration seconds. */
};

/****************************************************************************
 * The module global table of Session Registries
 ****************************************************************************/

/* name -> AltpSessionRegistry *, no reference held */
static GHashTable *altp_session_registries;
G_LOCK_DEFINE_STATIC(altp_session_registries);

static AltpSessionRegistry *
_registry_new(const gchar *name)
{
  AltpSessionRegistry *self = g_new0(AltpSessionRegistry, 1);

  self->ref_cnt = 1;
  self->name = g_strdup(name);
  g_mutex_init(&self->mutex);
  self->sessions = g_hash_table_new_full(g_str_hash, g_str_equal, NULL,
                                         (GDestroyNotify) altp_session_record_unref);

  return self;
}

static void
_registry_free(AltpSessionRegistry *self)
{
  g_hash_table_destroy(self->sessions);
  g_mutex_clear(&self->mutex);
  g_free(self->name);
  g_free(self);
}

AltpSessionRegistry *
altp_session_registry_ref_by_name(const gchar *name)
{
  AltpSessionRegistry *self;

  G_LOCK(altp_session_registries);
  if (!altp_session_registries)
    altp_session_registries = g_hash_table_new(g_str_hash, g_str_equal);

  self = g_hash_table_lookup(altp_session_registries, name);
  if (self)
    {
      self->ref_cnt++;
    }
  else
    {
      self = _registry_new(name);
      g_hash_table_insert(altp_session_registries, self->name, self);
      msg_debug("Creating the ALTP Session Registry of a Receiver",
                evt_tag_str("name", name));
    }
  G_UNLOCK(altp_session_registries);

  return self;
}

/* under the module global lock, so a lookup cannot resurrect a dying registry */
AltpSessionRegistry *
altp_session_registry_ref(AltpSessionRegistry *self)
{
  if (!self)
    return NULL;

  G_LOCK(altp_session_registries);
  self->ref_cnt++;
  G_UNLOCK(altp_session_registries);

  return self;
}

void
altp_session_registry_unref(AltpSessionRegistry *self)
{
  gboolean last_ref = FALSE;

  if (!self)
    return;

  G_LOCK(altp_session_registries);
  if (--self->ref_cnt == 0)
    {
      g_hash_table_remove(altp_session_registries, self->name);
      last_ref = TRUE;
    }
  G_UNLOCK(altp_session_registries);

  if (last_ref)
    _registry_free(self);
}

const gchar *
altp_session_registry_get_name(AltpSessionRegistry *self)
{
  return self->name;
}

guint
altp_session_registry_get_session_count(AltpSessionRegistry *self)
{
  guint count;

  g_mutex_lock(&self->mutex);
  count = g_hash_table_size(self->sessions);
  g_mutex_unlock(&self->mutex);

  return count;
}

/****************************************************************************
 * Session Record
 ****************************************************************************/

static AltpSessionRecord *
_record_new(const gchar *session_id)
{
  AltpSessionRecord *self = g_new0(AltpSessionRecord, 1);

  self->ref_cnt = 1;
  self->session_id = g_strdup(session_id);
  g_mutex_init(&self->mutex);
  self->batch_seq = 1;
  self->last_used = get_cached_realtime_sec();

  return self;
}

AltpSessionRecord *
altp_session_record_ref(AltpSessionRecord *self)
{
  if (!self)
    return NULL;

  g_atomic_int_inc(&self->ref_cnt);
  return self;
}

void
altp_session_record_unref(AltpSessionRecord *self)
{
  if (!self)
    return;

  if (!g_atomic_int_dec_and_test(&self->ref_cnt))
    return;

  g_mutex_clear(&self->mutex);
  g_free(self->session_id);
  g_free(self);
}

guint
altp_session_record_get_ref_count(AltpSessionRecord *self)
{
  return (guint) g_atomic_int_get(&self->ref_cnt);
}

const gchar *
altp_session_record_get_session_id(AltpSessionRecord *self)
{
  return self->session_id;
}

AltpSessionRecord *
altp_session_registry_lookup(AltpSessionRegistry *self, const gchar *session_id)
{
  AltpSessionRecord *record;

  g_mutex_lock(&self->mutex);
  record = g_hash_table_lookup(self->sessions, session_id);
  if (!record)
    {
      /* Stage B2: before creating a fresh Session Record, the persisted entry
       * "<name>.altp.session(<session-id>)" is looked up and loaded, so that
       * the Session resumes where its previous Connection left it (10.1). */
      record = _record_new(session_id);
      g_hash_table_insert(self->sessions, record->session_id, record);
      msg_debug("Creating an ALTP Session Record",
                evt_tag_str("receiver", self->name),
                evt_tag_str("session_id", session_id));
    }
  altp_session_record_ref(record);
  g_mutex_unlock(&self->mutex);

  return record;
}

/* The wakeup callback is called with the record mutex held on purpose: it
 * lives in the LogProtoServer of the Owning Connection, and only the mutex
 * keeps that Connection from freeing itself underneath us.
 * log_reader_wakeup() only posts an ivykis event, so it cannot deadlock.
 */
static void
_wake_owner_locked(AltpSessionRecord *self)
{
  if (self->owner && self->owner->wakeup)
    log_proto_server_wakeup_cb_call(self->owner->wakeup);
}

gboolean
altp_session_record_take_over(AltpSessionRecord *self, AltpSessionOwner *owner)
{
  gboolean displaced_another = FALSE;

  g_mutex_lock(&self->mutex);
  if (self->owner && self->owner != owner)
    {
      /* newest connection wins: the older Connection closes without
       * acknowledging anything, but its Frames stay counted and their
       * durability reports keep updating this Session Record (7.3) */
      self->owner->displaced = TRUE;
      self->owner->awaiting = FALSE;
      _wake_owner_locked(self);
      displaced_another = TRUE;
    }
  self->owner = owner;
  self->last_used = get_cached_realtime_sec();
  g_mutex_unlock(&self->mutex);

  return displaced_another;
}

void
altp_session_record_release(AltpSessionRecord *self, AltpSessionOwner *owner)
{
  g_mutex_lock(&self->mutex);
  if (self->owner == owner)
    self->owner = NULL;
  owner->awaiting = FALSE;
  g_mutex_unlock(&self->mutex);
}

gboolean
altp_session_record_is_displaced(AltpSessionRecord *self, AltpSessionOwner *owner)
{
  gboolean displaced;

  g_mutex_lock(&self->mutex);
  displaced = owner->displaced;
  g_mutex_unlock(&self->mutex);

  return displaced;
}

gboolean
altp_session_record_is_batch_durable(AltpSessionRecord *self)
{
  gboolean durable;

  g_mutex_lock(&self->mutex);
  durable = self->frames_acked == self->frames_read;
  g_mutex_unlock(&self->mutex);

  return durable;
}

gboolean
altp_session_record_wait_for_durability(AltpSessionRecord *self, AltpSessionOwner *owner, guint32 *frames_acked)
{
  gboolean durable;

  g_mutex_lock(&self->mutex);
  durable = self->frames_acked == self->frames_read;
  /* arming the wakeup and testing durability in one critical section is what
   * keeps a report arriving right now from being missed */
  owner->awaiting = !durable;
  *frames_acked = self->frames_acked;
  g_mutex_unlock(&self->mutex);

  return durable;
}

guint32
altp_session_record_acknowledge(AltpSessionRecord *self, AltpSessionOwner *owner, gboolean partial)
{
  guint32 frames_acked;

  g_mutex_lock(&self->mutex);
  if (partial)
    {
      /* The Frames beyond the durable prefix are disowned and the Sender
       * resends them in a new Batch, so bumping batch_seq is what makes their
       * later durability reports stale (9.3, 10.1).
       */
      self->frames_read = self->frames_acked;
      self->batch_seq++;
    }
  owner->awaiting = FALSE;
  frames_acked = self->frames_acked;
  self->last_used = get_cached_realtime_sec();
  /* Stage B2: persist the counters */
  g_mutex_unlock(&self->mutex);

  return frames_acked;
}

void
altp_session_record_reset_counters(AltpSessionRecord *self)
{
  g_mutex_lock(&self->mutex);
  self->frames_read = 0;
  self->frames_acked = 0;
  self->batch_seq++;
  self->last_used = get_cached_realtime_sec();
  /* Stage B2: persist the reset before the command line is processed */
  g_mutex_unlock(&self->mutex);
}

void
altp_session_record_note_frame_read(AltpSessionRecord *self, guint64 *batch_seq, guint32 *frame_index)
{
  g_mutex_lock(&self->mutex);
  *batch_seq = self->batch_seq;
  *frame_index = self->frames_read;
  self->frames_read++;
  self->last_used = get_cached_realtime_sec();
  /* Stage B2: persist frames_read before we rely on it (10.1) */
  g_mutex_unlock(&self->mutex);
}

void
altp_session_record_get_counters(AltpSessionRecord *self, guint32 *frames_read, guint32 *frames_acked)
{
  g_mutex_lock(&self->mutex);
  *frames_read = self->frames_read;
  *frames_acked = self->frames_acked;
  g_mutex_unlock(&self->mutex);
}

/****************************************************************************
 * Durability: the Bookmark of one Frame
 ****************************************************************************/

typedef struct _AltpBookmarkData
{
  AltpSessionRecord *record;
  guint64 batch_seq;
  guint32 frame_index;
} AltpBookmarkData;

G_STATIC_ASSERT(sizeof(AltpBookmarkData) <= sizeof(BookmarkContainer));

/* Runs on the destination thread, for the last Frame of every newly durable
 * prefix (consecutive_ack_tracker.c). */
static void
_altp_bookmark_save(Bookmark *bookmark)
{
  AltpBookmarkData *data = (AltpBookmarkData *) &bookmark->container;
  AltpSessionRecord *record = data->record;

  g_mutex_lock(&record->mutex);
  if (record->batch_seq != data->batch_seq)
    {
      msg_trace("Ignoring the durability report of an already acknowledged ALTP Batch",
                evt_tag_str("session_id", record->session_id),
                evt_tag_int("frame_index", data->frame_index));
      g_mutex_unlock(&record->mutex);
      return;
    }

  /* an absolute prefix count, so frames_acked never decreases in a Batch (10.1) */
  if (data->frame_index + 1 > record->frames_acked)
    record->frames_acked = data->frame_index + 1;
  record->last_used = get_cached_realtime_sec();
  /* Stage B2: persist frames_acked */

  msg_trace("ALTP Frames became durable",
            evt_tag_str("session_id", record->session_id),
            evt_tag_int("frames_acked", record->frames_acked),
            evt_tag_int("frames_read", record->frames_read));

  if (record->owner && record->owner->awaiting && record->frames_acked == record->frames_read)
    _wake_owner_locked(record);
  g_mutex_unlock(&record->mutex);
}

static void
_altp_bookmark_destroy(Bookmark *bookmark)
{
  AltpBookmarkData *data = (AltpBookmarkData *) &bookmark->container;

  altp_session_record_unref(data->record);
  data->record = NULL;
  bookmark->save = NULL;
  bookmark->destroy = NULL;
}

void
altp_session_bookmark_fill(Bookmark *bookmark, AltpSessionRecord *record, guint64 batch_seq, guint32 frame_index)
{
  AltpBookmarkData *data = (AltpBookmarkData *) &bookmark->container;

  /* the ack tracker hands out the same pending Bookmark again when the fetch()
   * it was requested for produced no message, so an earlier fill of ours may
   * still be in there with a reference of its own */
  if (bookmark->destroy == _altp_bookmark_destroy)
    _altp_bookmark_destroy(bookmark);

  data->record = altp_session_record_ref(record);
  data->batch_seq = batch_seq;
  data->frame_index = frame_index;
  bookmark->save = _altp_bookmark_save;
  bookmark->destroy = _altp_bookmark_destroy;
}
