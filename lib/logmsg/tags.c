/*
 * Copyright (c) 2009-2013 Balabit
 * Copyright (c) 2009 Marton Illes
 * Copyright (c) 2009-2013 Balázs Scheidler
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

#include "tags.h"
#include "messages.h"
#include "stats/stats-registry.h"
#include "stats/stats-cluster-single.h"
#include "apphook.h"

typedef struct _LogTag
{
  LogTagId id;
  gchar *name;
  StatsCounterItem *counter;
} LogTag;

static LogTag *log_tags[LOG_TAGS_MAX];
static guint log_tags_num;
static GHashTable *log_tags_hash = NULL;
static GMutex log_tags_lock;

static inline LogTag *
_lookup_tag(LogTagId id)
{
  if (id >= LOG_TAGS_MAX)
    return NULL;
  return g_atomic_pointer_get(&log_tags[id]);
}

/* NOTE: needs both log_tags_lock and the stats lock */
static void
_tag_update_counter_registration(LogTag *tag, gboolean enable)
{
  StatsClusterKey sc_key;
  StatsClusterLabel labels[] = { stats_cluster_label("id", tag->name) };

  if (enable == !!(tag->counter))
    return;

  stats_cluster_single_key_set(&sc_key, METRIC(tagged_events_total), labels, G_N_ELEMENTS(labels));
  stats_cluster_single_key_add_legacy_alias_with_name(&sc_key, SCS_TAG, tag->name, NULL, "processed");

  if (enable)
    {
      stats_register_counter(3, &sc_key, SC_TYPE_SINGLE_VALUE, &tag->counter);
    }
  else
    {
      stats_unregister_counter(&sc_key, SC_TYPE_SINGLE_VALUE, &tag->counter);
    }
}

static guint
_register_tag(const gchar *name, guint id)
{
  g_assert(id < LOG_TAGS_MAX);
  g_assert(log_tags[id] == NULL);

  LogTag *tag = g_new0(LogTag, 1);
  tag->id = id;
  tag->name = g_strdup(name);

  /* NOTE: stats-level may not be set for calls that happen during
   * config file parsing, those get fixed up by
   * log_tags_reinit_stats() below */

  stats_lock();
  _tag_update_counter_registration(tag, stats_check_level(3));
  stats_unlock();

  g_hash_table_insert(log_tags_hash, tag->name, GUINT_TO_POINTER(id + 1));
  if (id >= log_tags_num)
    log_tags_num = id + 1;

  /* publish last, so that lockless readers only ever see a complete tag */
  g_atomic_pointer_set(&log_tags[id], tag);
  return id;
}

static guint
_register_new_tag(const gchar *name)
{
  guint id = log_tags_num;
  return _register_tag(name, id);
}

/*
 * log_tags_get_by_name
 *
 * Lookup a tag id by it's name. If the tag is seen for the first time
 * the next tag id is assigned and the tag is added to the list.
 *
 * The function returns the tag id associated with the name.
 *
 * @name:   the name of the tag
 *
 */
LogTagId
log_tags_get_by_name(const gchar *name)
{
  /* If log_tags_hash() is NULL, this unit is already deinitialized
     but other thread may refer the tag structure.

     If name is empty, it is an extremal element.

     In both cases the return value is 0.
   */
  guint id = 0;

  g_assert(log_tags_hash != NULL);

  g_mutex_lock(&log_tags_lock);

  gpointer key = g_hash_table_lookup(log_tags_hash, name);
  if (!key)
    {
      if (log_tags_num < LOG_TAGS_MAX - 1)
        {
          id = _register_new_tag(name);
        }
      else
        id = 0;
    }
  else
    {
      id = GPOINTER_TO_UINT(key) - 1;
    }

  g_mutex_unlock(&log_tags_lock);

  return id;
}

void
log_tags_register_predefined_tag(const gchar *name, LogTagId id)
{
  g_mutex_lock(&log_tags_lock);

  gpointer key = g_hash_table_lookup(log_tags_hash, name);
  g_assert(key == NULL);

  LogTagId rid = _register_tag(name, id);
  g_assert(rid == id);
  g_mutex_unlock(&log_tags_lock);
}

/*
 * log_tag_get_by_id
 *
 * Lookup a tag name by it's id. If the id is invalid
 * NULL is returned, otherwise a gchar * is returned
 * pointing to the name of the tag.
 *
 * The returned pointer should not be freed.
 *
 * @id:     the tag id to lookup
 *
 */
const gchar *
log_tags_get_by_id(LogTagId id)
{
  LogTag *tag = _lookup_tag(id);

  return tag ? tag->name : NULL;
}

void
log_tags_inc_counter(LogTagId id)
{
  LogTag *tag = _lookup_tag(id);

  if (tag)
    stats_counter_inc(tag->counter);
}

void
log_tags_dec_counter(LogTagId id)
{
  LogTag *tag = _lookup_tag(id);

  if (tag)
    stats_counter_dec(tag->counter);
}

/*
 * NOTE: this is called from the AH_CONFIG_CHANGED hook to update the set of
 * counters we have.  If stats-level is decreased, we should unregister
 * everything we had earlier. If increased we need to register them again.
 *
 * log_tags_get_by_name() will also try to register the counter for calls
 * that are _after_ cfg_init().  Early calls to log_tags_get_by_name() will
 * not see a proper stats-level() in the global variable here.  Those will
 * get handled by this function.
 *
 * The counter pointers are rewritten here while log_tags_inc_counter() and
 * friends read them without a lock.  That is safe because the hook runs
 * before the threaded workers of the new configuration are started and
 * while the main loop is not processing messages, so no message is in
 * flight.
 */
void
log_tags_reinit_stats(void)
{
  g_mutex_lock(&log_tags_lock);
  stats_lock();

  gboolean enable = stats_check_level(3);
  for (guint id = 0; id < log_tags_num; id++)
    {
      LogTag *tag = log_tags[id];

      if (tag)
        _tag_update_counter_registration(tag, enable);
    }

  stats_unlock();
  g_mutex_unlock(&log_tags_lock);
}

void
log_tags_global_init(void)
{
  log_tags_hash = g_hash_table_new(g_str_hash, g_str_equal);

  register_application_hook(AH_CONFIG_CHANGED, (ApplicationHookFunc) log_tags_reinit_stats, NULL, AHM_RUN_REPEAT);
}

void
log_tags_global_deinit(void)
{
  g_hash_table_destroy(log_tags_hash);
  log_tags_hash = NULL;

  stats_lock();
  for (guint id = 0; id < log_tags_num; id++)
    {
      LogTag *tag = log_tags[id];

      if (!tag)
        continue;

      _tag_update_counter_registration(tag, FALSE);
      g_free(tag->name);
      g_free(tag);
      log_tags[id] = NULL;
    }
  stats_unlock();
  log_tags_num = 0;
}
