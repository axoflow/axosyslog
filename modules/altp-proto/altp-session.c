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
#include "mainloop-call.h"
#include "messages.h"
#include "persistable-state-header.h"
#include "timeutils/cache.h"
#include "timeutils/misc.h"

#include <iv.h>

#include <string.h>

/* The persisted form of a Session Record (specification 10.1).  The explicit
 * padding keeps the layout free of implicit holes, so that it is the same on
 * every ABI a persist file may travel between; the byte order is recorded in
 * the header.
 */
typedef struct _AltpSessionPersistedState
{
  PersistableStateHeader header;
  guint8 __padding1[2];
  guint32 frames_read;
  guint32 frames_acked;
  guint32 __padding2;
  gint64 last_used;
} AltpSessionPersistedState;

G_STATIC_ASSERT(sizeof(AltpSessionPersistedState) == 24);

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

  /* The "<registry name>.altp.session(<session-id>)" entry of the Session,
   * NULL while the Session Record is memory only (a unit test).  Every counter
   * update below writes the entry through before it returns, so the counters
   * are persisted before the Receiver relies on them (10.1).
   */
  PersistState *persist_state;
  PersistEntryHandle persist_handle;
};

struct _AltpSessionRegistry
{
  gint ref_cnt;

  /* the persistent name of the Receiver driver: what scopes us (ADR-0005) */
  gchar *name;
  /* "<name>.altp.session(", the prefix of every entry of this Receiver */
  gchar *entry_prefix;

  GMutex mutex;
  /* Session ID -> AltpSessionRecord *, one reference held: the table is what
   * keeps a Session Record alive between two Connections of the Session.  A
   * record deliberately holds no reference back -- that would be a cycle
   * neither side could break.
   */
  GHashTable *sessions;

  /* the size of @sessions, as stats_register_external_counter() reads a gauge */
  atomic_gssize session_count;

  /* The persistent state of the configuration, NULL while the registry is
   * memory only.  It is bound once and stays valid for the lifetime of the
   * process: cfg_persist_config_move() hands the very same one to a reload.
   */
  PersistState *persist_state;
  gboolean bound;
  gint session_expiration;
  /* the largest number of Session Records we keep, G_MAXINT for unlimited */
  gint max_sessions;
  /* when a refusal was last logged: a bound under attack must not log per
   * Connection */
  gint64 last_refusal_logged;

  /* the periodic expiry sweep, owned by the main thread */
  struct iv_timer expiry_timer;
};

/* The structural operations of the persistent state -- allocating, looking up
 * and removing an entry -- mutate its key table, which is main thread only by
 * contract (see the note on _add_key() in lib/persist-state.c).  Serialising
 * ourselves against ourselves is not enough, as other components allocate
 * entries of their own on the main thread at runtime, so anything of ours that
 * touches a key asserts that it runs there, and
 * altp_session_registry_lookup() is where the marshalling happens.  Writing
 * the counters of an already allocated entry needs none of this:
 * persist_state_map_entry() is safe from any thread.
 */

/****************************************************************************
 * The persisted Session Record
 ****************************************************************************/

static void
_persisted_state_swap_byte_order(AltpSessionPersistedState *state)
{
  state->header.big_endian = !state->header.big_endian;
  state->frames_read = GUINT32_SWAP_LE_BE(state->frames_read);
  state->frames_acked = GUINT32_SWAP_LE_BE(state->frames_acked);
  state->last_used = (gint64) GUINT64_SWAP_LE_BE((guint64) state->last_used);
}

/* Bring an entry written by a CPU of the opposite byte order into ours -- the
 * very trick logproto-buffered-server.c plays. */
static void
_persisted_state_normalize_byte_order(AltpSessionPersistedState *state)
{
  if ((state->header.big_endian && G_BYTE_ORDER == G_LITTLE_ENDIAN) ||
      (!state->header.big_endian && G_BYTE_ORDER == G_BIG_ENDIAN))
    _persisted_state_swap_byte_order(state);
}

static void
_persisted_state_init(AltpSessionPersistedState *state, guint32 frames_read, guint32 frames_acked, gint64 last_used)
{
  memset(state, 0, sizeof(*state));
  state->header.version = ALTP_SESSION_PERSIST_VERSION;
  state->header.big_endian = (G_BYTE_ORDER == G_BIG_ENDIAN);
  state->frames_read = frames_read;
  state->frames_acked = frames_acked;
  state->last_used = last_used;
}

/* Write the counters through with the record mutex held: a map, a handful of
 * stores and an unmap, so the mapping is never held across anything blocking. */
static void
_record_persist_locked(AltpSessionRecord *self)
{
  if (!self->persist_handle)
    return;

  AltpSessionPersistedState *state = persist_state_map_entry(self->persist_state, self->persist_handle);

  _persisted_state_init(state, self->frames_read, self->frames_acked, self->last_used);
  persist_state_unmap_entry(self->persist_state, self->persist_handle);
}

/****************************************************************************
 * The module global table of Session Registries
 ****************************************************************************/

/* name -> AltpSessionRegistry *, no reference held */
static GHashTable *altp_session_registries;
G_LOCK_DEFINE_STATIC(altp_session_registries);

static void _registry_expiry_timer_expired(gpointer cookie);

static AltpSessionRegistry *
_registry_new(const gchar *name)
{
  AltpSessionRegistry *self = g_new0(AltpSessionRegistry, 1);

  self->ref_cnt = 1;
  self->name = g_strdup(name);
  self->entry_prefix = g_strdup_printf("%s.altp.session(", name);
  g_mutex_init(&self->mutex);
  self->sessions = g_hash_table_new_full(g_str_hash, g_str_equal, NULL,
                                         (GDestroyNotify) altp_session_record_unref);
  atomic_gssize_set(&self->session_count, 0);
  /* nothing expires and nothing is refused until a Receiver bound us */
  self->session_expiration = G_MAXINT;
  self->max_sessions = G_MAXINT;

  IV_TIMER_INIT(&self->expiry_timer);
  self->expiry_timer.cookie = self;
  self->expiry_timer.handler = _registry_expiry_timer_expired;

  return self;
}

static void
_registry_free(AltpSessionRegistry *self)
{
  /* the sweep timer is registered on the main thread, where we are freed too */
  if (iv_timer_registered(&self->expiry_timer))
    iv_timer_unregister(&self->expiry_timer);

  g_hash_table_destroy(self->sessions);
  g_mutex_clear(&self->mutex);
  g_free(self->entry_prefix);
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

gint
altp_session_registry_get_max_sessions(AltpSessionRegistry *self)
{
  return self->max_sessions;
}

atomic_gssize *
altp_session_registry_get_session_count_ref(AltpSessionRegistry *self)
{
  return &self->session_count;
}

gboolean
altp_session_registry_should_log_refusal(AltpSessionRegistry *self)
{
  gint64 now = get_cached_realtime_sec();
  gboolean should_log;

  g_mutex_lock(&self->mutex);
  should_log = now - self->last_refusal_logged >= (gint64) ALTP_SESSION_REFUSAL_LOG_INTERVAL;
  if (should_log)
    self->last_refusal_logged = now;
  g_mutex_unlock(&self->mutex);

  return should_log;
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

/****************************************************************************
 * Loading and forgetting a persisted Session Record
 ****************************************************************************/

static gchar *
_format_entry_name(AltpSessionRegistry *self, const gchar *session_id)
{
  return g_strdup_printf("%s%s)", self->entry_prefix, session_id);
}

/* Read the counters of @handle into @record.  FALSE when the entry is not one
 * of ours, which 10.1 lets us treat as a Session whose state was lost.
 */
static gboolean
_record_load_from_entry(AltpSessionRecord *record, PersistState *persist_state, PersistEntryHandle handle,
                        gsize size, const gchar *entry_name)
{
  if (size != sizeof(AltpSessionPersistedState))
    {
      msg_warning("The persisted ALTP Session Record has an unexpected size, starting the Session over",
                  evt_tag_str("entry", entry_name),
                  evt_tag_int("size", size),
                  evt_tag_int("expected_size", sizeof(AltpSessionPersistedState)));
      return FALSE;
    }

  AltpSessionPersistedState *state = persist_state_map_entry(persist_state, handle);

  /* a single octet, so it is readable before the byte order of the rest is */
  if (state->header.version != ALTP_SESSION_PERSIST_VERSION)
    {
      msg_warning("The persisted ALTP Session Record has an unknown version, starting the Session over",
                  evt_tag_str("entry", entry_name),
                  evt_tag_int("version", state->header.version),
                  evt_tag_int("expected_version", ALTP_SESSION_PERSIST_VERSION));
      persist_state_unmap_entry(persist_state, handle);
      return FALSE;
    }

  _persisted_state_normalize_byte_order(state);
  record->frames_read = state->frames_read;
  record->frames_acked = state->frames_acked;
  record->last_used = state->last_used;
  persist_state_unmap_entry(persist_state, handle);

  return TRUE;
}

/* Attach the persisted entry to a freshly created Session Record: our own
 * entries are loaded, anything else is replaced by one with zero counters. */
static void
_record_attach_entry(AltpSessionRegistry *self, AltpSessionRecord *record)
{
  gchar *entry_name = _format_entry_name(self, record->session_id);
  gsize size = 0;
  guint8 entry_format_version = 0;

  main_loop_assert_main_thread();

  PersistEntryHandle handle = persist_state_lookup_entry(self->persist_state, entry_name, &size,
                                                         &entry_format_version);

  if (handle && _record_load_from_entry(record, self->persist_state, handle, size, entry_name))
    {
      msg_debug("Resuming an ALTP Session from the persistent state of the Receiver",
                evt_tag_str("receiver", self->name),
                evt_tag_str("session_id", record->session_id),
                evt_tag_int("frames_read", record->frames_read),
                evt_tag_int("frames_acked", record->frames_acked));
    }
  else
    {
      /* alloc replaces an entry of the same name, which is how a refused one
       * is discarded */
      handle = persist_state_alloc_entry(self->persist_state, entry_name, sizeof(AltpSessionPersistedState));
      if (handle)
        {
          AltpSessionPersistedState *state = persist_state_map_entry(self->persist_state, handle);

          _persisted_state_init(state, 0, 0, record->last_used);
          persist_state_unmap_entry(self->persist_state, handle);
        }
      else
        {
          msg_error("Cannot allocate the persistent state of an ALTP Session, "
                    "the Session will not survive a restart of the Receiver",
                    evt_tag_str("entry", entry_name));
        }
    }

  record->persist_state = handle ? self->persist_state : NULL;
  record->persist_handle = handle;
  g_free(entry_name);
}

/* Forget the persisted Session Record of @entry_name.
 *
 * persist_state_remove_entry() only clears the in-use bit, so a lookup of the
 * same name revives the entry until the persist file is rewritten on the next
 * startup.  The counters are zeroed first, so that a Session coming back
 * before that is answered `250 Received 0` as one whose state was lost (10.1).
 */
static void
_forget_entry(PersistState *persist_state, const gchar *entry_name)
{
  gsize size = 0;
  guint8 entry_format_version = 0;

  main_loop_assert_main_thread();

  PersistEntryHandle handle = persist_state_lookup_entry(persist_state, entry_name, &size, &entry_format_version);
  if (handle && size == sizeof(AltpSessionPersistedState))
    {
      AltpSessionPersistedState *state = persist_state_map_entry(persist_state, handle);

      _persisted_state_init(state, 0, 0, 0);
      persist_state_unmap_entry(persist_state, handle);
    }
  persist_state_remove_entry(persist_state, entry_name);
}

typedef struct _AltpLookupRequest
{
  AltpSessionRegistry *registry;
  const gchar *session_id;
  /* out: one reference of the Session Record of @session_id */
  AltpSessionRecord *record;
} AltpLookupRequest;

/* Creates the Session Record of a Session that is not in memory and binds its
 * persisted entry.  The whole slow path is marshalled and not just the entry
 * binding, so that two Connections of the same Session arriving at once cannot
 * allocate the very same entry twice.
 */
static gpointer
_lookup_or_create_record(gpointer args)
{
  AltpLookupRequest *request = (AltpLookupRequest *) args;
  AltpSessionRegistry *self = request->registry;

  g_mutex_lock(&self->mutex);
  AltpSessionRecord *record = g_hash_table_lookup(self->sessions, request->session_id);
  if (!record && g_hash_table_size(self->sessions) >= (guint) self->max_sessions)
    {
      /* At the max-sessions() bound no Session Record is created and the
       * Connection closes without a reply.  No established Session is evicted
       * to make room, or an attacker could churn the state of the legitimate
       * Senders; the expiry sweep stays the only thing that frees a record.
       */
      g_mutex_unlock(&self->mutex);
      return NULL;
    }

  if (!record)
    {
      record = _record_new(request->session_id);
      if (self->persist_state)
        _record_attach_entry(self, record);
      g_hash_table_insert(self->sessions, record->session_id, record);
      atomic_gssize_set(&self->session_count, g_hash_table_size(self->sessions));
      msg_debug("Creating an ALTP Session Record",
                evt_tag_str("receiver", self->name),
                evt_tag_str("session_id", request->session_id));
    }
  request->record = altp_session_record_ref(record);
  g_mutex_unlock(&self->mutex);

  return NULL;
}

AltpSessionRecord *
altp_session_registry_lookup(AltpSessionRegistry *self, const gchar *session_id)
{
  AltpSessionRecord *record;

  /* the fast path of every SYNC of a Session that already has a Session
   * Record: it touches no key of the persistent state, so no main thread */
  g_mutex_lock(&self->mutex);
  record = altp_session_record_ref(g_hash_table_lookup(self->sessions, session_id));
  g_mutex_unlock(&self->mutex);

  if (record)
    return record;

  /* Creating one touches a key of the persistent state, which is main thread
   * only (see the note above), while a SYNC arrives in fetch() -- on an I/O
   * worker with flags(threaded).  main_loop_call() runs it right here when we
   * are the main thread already and otherwise blocks us until it ran; the main
   * loop keeps serving its event queue meanwhile, so this cannot deadlock.
   *
   * No lock of ours may be held across the call: the main thread takes the
   * registry mutex and the mutex of a Session Record itself.
   */
  AltpLookupRequest request =
  {
    .registry = self,
    .session_id = session_id,
    .record = NULL,
  };

  main_loop_call((MainLoopTaskFunc) _lookup_or_create_record, &request, TRUE);

  /* NULL at the max-sessions() bound: only creation is refused, never use */
  return request.record;
}

/****************************************************************************
 * The prefix sweep: the restart rule of specification 10.1 and expiry
 ****************************************************************************/

/* One entry of the Receiver as the foreach below found it.  Entries are
 * collected first and acted upon afterwards, because
 * persist_state_foreach_entry() walks the very key table a removal mutates.
 */
typedef struct _AltpEntrySnapshot
{
  gchar *name;
  guint32 frames_read;
  guint32 frames_acked;
  gint64 last_used;
  /* FALSE for an entry that carries a layout we do not know */
  gboolean readable;
} AltpEntrySnapshot;

static void
_entry_snapshot_free(gpointer data)
{
  AltpEntrySnapshot *self = (AltpEntrySnapshot *) data;

  g_free(self->name);
  g_free(self);
}

typedef struct _AltpSweepCollector
{
  AltpSessionRegistry *registry;
  GPtrArray *entries;
} AltpSweepCollector;

static void
_collect_entry(gchar *name, gint size, gpointer entry, gpointer userdata)
{
  AltpSweepCollector *collector = (AltpSweepCollector *) userdata;
  AltpSessionRegistry *self = collector->registry;
  gsize prefix_len = strlen(self->entry_prefix);
  gsize name_len = strlen(name);

  if (name_len <= prefix_len || strncmp(name, self->entry_prefix, prefix_len) != 0
      || name[name_len - 1] != ')')
    return;

  AltpEntrySnapshot *snapshot = g_new0(AltpEntrySnapshot, 1);

  snapshot->name = g_strdup(name);

  if (size > 0 && (gsize) size == sizeof(AltpSessionPersistedState))
    {
      AltpSessionPersistedState *state = (AltpSessionPersistedState *) entry;

      if (state->header.version == ALTP_SESSION_PERSIST_VERSION)
        {
          _persisted_state_normalize_byte_order(state);
          snapshot->frames_read = state->frames_read;
          snapshot->frames_acked = state->frames_acked;
          snapshot->last_used = state->last_used;
          snapshot->readable = TRUE;
        }
    }

  g_ptr_array_add(collector->entries, snapshot);
}

static GPtrArray *
_collect_entries(AltpSessionRegistry *self)
{
  AltpSweepCollector collector =
  {
    .registry = self,
    .entries = g_ptr_array_new_with_free_func(_entry_snapshot_free),
  };

  main_loop_assert_main_thread();

  persist_state_foreach_entry(self->persist_state, _collect_entry, &collector);

  return collector.entries;
}

static gboolean
_is_expired(AltpSessionRegistry *self, gint64 last_used, gint64 now)
{
  return now - last_used >= (gint64) self->session_expiration;
}

static void
_rewind_entry(AltpSessionRegistry *self, AltpEntrySnapshot *snapshot)
{
  gsize size = 0;
  guint8 entry_format_version = 0;

  main_loop_assert_main_thread();

  PersistEntryHandle handle = persist_state_lookup_entry(self->persist_state, snapshot->name, &size,
                                                         &entry_format_version);
  if (handle && size == sizeof(AltpSessionPersistedState))
    {
      AltpSessionPersistedState *state = persist_state_map_entry(self->persist_state, handle);

      _persisted_state_init(state, snapshot->frames_acked, snapshot->frames_acked, snapshot->last_used);
      persist_state_unmap_entry(self->persist_state, handle);
    }
}

/* The restart rule of specification 10.1: Frames read but not durable before
 * the Receiver stopped were in memory only, so every Session resumes at its
 * durable prefix.  Entries unused for session_expiration are forgotten.
 */
static void
_apply_restart_rule(AltpSessionRegistry *self)
{
  GPtrArray *entries = _collect_entries(self);
  gint64 now = get_cached_realtime_sec();
  guint expired = 0, rewound = 0;

  for (guint i = 0; i < entries->len; i++)
    {
      AltpEntrySnapshot *snapshot = (AltpEntrySnapshot *) g_ptr_array_index(entries, i);

      if (!snapshot->readable || _is_expired(self, snapshot->last_used, now))
        {
          _forget_entry(self->persist_state, snapshot->name);
          expired++;
          continue;
        }

      if (snapshot->frames_read == snapshot->frames_acked)
        continue;

      _rewind_entry(self, snapshot);
      rewound++;
    }

  msg_debug("Applied the restart rule to the persisted ALTP Sessions of a Receiver",
            evt_tag_str("receiver", self->name),
            evt_tag_int("entries", entries->len),
            evt_tag_int("expired", expired),
            evt_tag_int("rewound", rewound));

  g_ptr_array_free(entries, TRUE);
}

/* An in memory Session Record nothing refers to any more -- no Owning
 * Connection, no Bookmark of an in-flight Frame -- and unused for
 * session_expiration seconds is dropped with its persisted entry. */
static GPtrArray *
_collect_expired_records(AltpSessionRegistry *self, gint64 now)
{
  GPtrArray *expired = g_ptr_array_new_with_free_func(g_free);
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init(&iter, self->sessions);
  while (g_hash_table_iter_next(&iter, &key, &value))
    {
      AltpSessionRecord *record = (AltpSessionRecord *) value;

      /* the table is the only holder of a record nothing else refers to */
      if (g_atomic_int_get(&record->ref_cnt) != 1)
        continue;

      g_mutex_lock(&record->mutex);
      gboolean drop = !record->owner && _is_expired(self, record->last_used, now);
      g_mutex_unlock(&record->mutex);

      if (drop)
        g_ptr_array_add(expired, g_strdup(record->session_id));
    }

  return expired;
}

static void
_expire_records(AltpSessionRegistry *self)
{
  gint64 now = get_cached_realtime_sec();

  g_mutex_lock(&self->mutex);
  GPtrArray *expired = _collect_expired_records(self, now);

  for (guint i = 0; i < expired->len; i++)
    {
      const gchar *session_id = (const gchar *) g_ptr_array_index(expired, i);

      if (self->persist_state)
        {
          gchar *entry_name = _format_entry_name(self, session_id);

          _forget_entry(self->persist_state, entry_name);
          g_free(entry_name);
        }

      msg_debug("Expiring an unused ALTP Session Record",
                evt_tag_str("receiver", self->name),
                evt_tag_str("session_id", session_id),
                evt_tag_int("session_expiration", self->session_expiration));
      g_hash_table_remove(self->sessions, session_id);
    }
  atomic_gssize_set(&self->session_count, g_hash_table_size(self->sessions));
  g_mutex_unlock(&self->mutex);

  g_ptr_array_free(expired, TRUE);
}

static void
_registry_arm_expiry_timer(AltpSessionRegistry *self)
{
  iv_validate_now();
  self->expiry_timer.expires = iv_now;
  timespec_add_msec(&self->expiry_timer.expires, (gint64) ALTP_SESSION_EXPIRY_SWEEP_INTERVAL * 1000);
  iv_timer_register(&self->expiry_timer);
}

static void
_registry_expiry_timer_expired(gpointer cookie)
{
  AltpSessionRegistry *self = (AltpSessionRegistry *) cookie;

  _expire_records(self);
  _registry_arm_expiry_timer(self);
}

void
altp_session_registry_fire_expiry(AltpSessionRegistry *self)
{
  _expire_records(self);
}

/* max-sessions(0) is unlimited, normalized here so that the smaller-value-wins
 * rule below is a plain comparison */
static gint
_normalize_max_sessions(gint max_sessions)
{
  return max_sessions > 0 ? max_sessions : G_MAXINT;
}

/* The registry is per persistent name while the options are per driver, so the
 * smaller value of the two applies to the Session Records they share. */
static void
_rebind_session_expiration(AltpSessionRegistry *self, gint session_expiration)
{
  if (session_expiration < self->session_expiration)
    {
      msg_notice("An ALTP Receiver bound a Session Registry that another one holds with a larger "
                 "session-expiration(), the smaller value applies to the whole Receiver",
                 evt_tag_str("receiver", self->name),
                 evt_tag_int("session_expiration", session_expiration),
                 evt_tag_int("previous_session_expiration", self->session_expiration));
      self->session_expiration = session_expiration;
    }
  else if (session_expiration > self->session_expiration)
    {
      msg_notice("An ALTP Receiver bound a Session Registry that another one holds with a smaller "
                 "session-expiration(), the smaller value stays in effect for the whole Receiver",
                 evt_tag_str("receiver", self->name),
                 evt_tag_int("session_expiration", session_expiration),
                 evt_tag_int("session_expiration_in_effect", self->session_expiration));
    }
}

static void
_rebind_max_sessions(AltpSessionRegistry *self, gint max_sessions)
{
  gint normalized = _normalize_max_sessions(max_sessions);

  if (normalized < self->max_sessions)
    {
      msg_notice("An ALTP Receiver bound a Session Registry that another one holds with a larger "
                 "max-sessions(), the smaller value applies to the whole Receiver",
                 evt_tag_str("receiver", self->name),
                 evt_tag_int("max_sessions", normalized),
                 evt_tag_int("previous_max_sessions", self->max_sessions));
      self->max_sessions = normalized;
    }
  else if (normalized > self->max_sessions)
    {
      msg_notice("An ALTP Receiver bound a Session Registry that another one holds with a smaller "
                 "max-sessions(), the smaller value stays in effect for the whole Receiver",
                 evt_tag_str("receiver", self->name),
                 evt_tag_int("max_sessions", normalized),
                 evt_tag_int("max_sessions_in_effect", self->max_sessions));
    }
}

void
altp_session_registry_bind_persist_state(AltpSessionRegistry *self, PersistState *state, gint session_expiration,
                                         gint max_sessions)
{
  if (self->bound)
    {
      /* A configuration reload binds again with the options of the driver it
       * recreated.  The registry is what it was, so the restart rule must not
       * run again and only the smaller of the two values is honoured.
       */
      _rebind_session_expiration(self, session_expiration);
      _rebind_max_sessions(self, max_sessions);
      return;
    }

  self->bound = TRUE;
  self->session_expiration = session_expiration;
  self->max_sessions = _normalize_max_sessions(max_sessions);
  self->persist_state = state;

  if (state)
    _apply_restart_rule(self);

  _registry_arm_expiry_timer(self);
}

/****************************************************************************
 * The Session Record of a live Connection
 ****************************************************************************/

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
altp_session_record_take_over(AltpSessionRecord *self, AltpSessionOwner *owner, const gchar *peer_address,
                              gchar *displaced_peer_address, gsize displaced_peer_address_len)
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
      if (displaced_peer_address)
        g_strlcpy(displaced_peer_address, self->owner->peer_address, displaced_peer_address_len);
      _wake_owner_locked(self);
      displaced_another = TRUE;
    }
  g_strlcpy(owner->peer_address, peer_address, sizeof(owner->peer_address));
  self->owner = owner;
  self->last_used = get_cached_realtime_sec();
  _record_persist_locked(self);
  g_mutex_unlock(&self->mutex);

  return displaced_another;
}

gboolean
altp_session_record_get_owner_peer_address(AltpSessionRecord *self, gchar *peer_address, gsize peer_address_len)
{
  gboolean owned;

  g_mutex_lock(&self->mutex);
  owned = self->owner != NULL;
  if (owned)
    g_strlcpy(peer_address, self->owner->peer_address, peer_address_len);
  g_mutex_unlock(&self->mutex);

  return owned;
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
  _record_persist_locked(self);
  g_mutex_unlock(&self->mutex);

  return frames_acked;
}

void
altp_session_record_abandon(AltpSessionRecord *self, AltpSessionOwner *owner)
{
  g_mutex_lock(&self->mutex);
  owner->awaiting = FALSE;
  g_mutex_unlock(&self->mutex);
}

void
altp_session_record_reset_counters(AltpSessionRecord *self)
{
  g_mutex_lock(&self->mutex);
  self->frames_read = 0;
  self->frames_acked = 0;
  self->batch_seq++;
  self->last_used = get_cached_realtime_sec();
  /* persisted before the command line that caused it is processed (10.1) */
  _record_persist_locked(self);
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
  /* frames_read is persisted before the Receiver relies on it (10.1) */
  _record_persist_locked(self);
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
  _record_persist_locked(record);

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
