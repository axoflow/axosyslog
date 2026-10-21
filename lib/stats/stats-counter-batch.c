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

#define STATS_COUNTER_BATCH_SIZE 16

typedef struct _StatsCounterBatchEntry
{
  StatsCounterItem *counter;
  gssize delta;
} StatsCounterBatchEntry;

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

static void
_batch_flush(void)
{
  for (gint i = 0; i < stats_batch_len; i++)
    stats_counter_add(stats_batch_entries[i].counter, stats_batch_entries[i].delta);
  stats_batch_len = 0;
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
stats_counter_add_batched(StatsCounterItem *counter, gssize add)
{
  if (!counter)
    return;

  if (stats_batch_depth == 0)
    {
      stats_counter_add(counter, add);
      return;
    }

  for (gint i = 0; i < stats_batch_len; i++)
    {
      if (stats_batch_entries[i].counter == counter)
        {
          stats_batch_entries[i].delta += add;
          return;
        }
    }

  if (stats_batch_len == STATS_COUNTER_BATCH_SIZE)
    _batch_flush();

  stats_batch_entries[stats_batch_len].counter = counter;
  stats_batch_entries[stats_batch_len].delta = add;
  stats_batch_len++;
}
