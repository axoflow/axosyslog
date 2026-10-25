/*
 * Copyright (c) 2026 Axoflow
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
#include "stats/stats-counter-batch.h"
#include "tls-support.h"

#define STATS_COUNTER_BATCH_BITS 5
#define STATS_COUNTER_BATCH_SIZE (1 << STATS_COUNTER_BATCH_BITS)

typedef struct _StatsCounterBatchEntry
{
  StatsCounterItem *counter;
  gssize delta;
} StatsCounterBatchEntry;

/*
 * The entries form a hash table keyed by the counter pointer.  A batch
 * typically has fewer than a dozen counters, but they are updated for every
 * message, so this is cheaper than scanning a list.
 */
TLS_BLOCK_START
{
  gint stats_batch_depth;
  gint stats_batch_len;
  StatsCounterBatchEntry stats_batch_entries[STATS_COUNTER_BATCH_SIZE];
}
TLS_BLOCK_END;

#define stats_batch_depth   __tls_deref(stats_batch_depth)
#define stats_batch_len     __tls_deref(stats_batch_len)
#define stats_batch_entries __tls_deref(stats_batch_entries)

static inline guint
_home_slot(StatsCounterItem *counter)
{
  /* Fibonacci hashing: the top bits of the product depend on all bits of
   * the pointer, so counters allocated next to each other spread out */
  return ((guint64) (guintptr) counter * G_GUINT64_CONSTANT(0x9E3779B97F4A7C15)) >> (64 - STATS_COUNTER_BATCH_BITS);
}

static void
_batch_flush(void)
{
  for (gint i = 0; i < STATS_COUNTER_BATCH_SIZE && stats_batch_len > 0; i++)
    {
      StatsCounterBatchEntry *entry = &stats_batch_entries[i];

      if (!entry->counter)
        continue;

      stats_counter_add(entry->counter, entry->delta);
      entry->counter = NULL;
      stats_batch_len--;
    }
}

void
stats_counter_batch_begin(void)
{
  stats_batch_depth++;
}

void
stats_counter_batch_end(void)
{
  g_assert(stats_batch_depth > 0);
  if (--stats_batch_depth == 0)
    _batch_flush();
}

void
_stats_counter_add_batched(StatsCounterItem *counter, gssize add)
{
  if (stats_batch_depth == 0)
    {
      stats_counter_add(counter, add);
      return;
    }

  guint slot = _home_slot(counter);
  for (gint probe = 0; probe < STATS_COUNTER_BATCH_SIZE; probe++)
    {
      StatsCounterBatchEntry *entry = &stats_batch_entries[(slot + probe) & (STATS_COUNTER_BATCH_SIZE - 1)];

      if (entry->counter == counter)
        {
          entry->delta += add;
          return;
        }
      if (!entry->counter)
        {
          entry->counter = counter;
          entry->delta = add;
          stats_batch_len++;
          return;
        }
    }

  _batch_flush();
  stats_batch_entries[slot].counter = counter;
  stats_batch_entries[slot].delta = add;
  stats_batch_len++;
}
