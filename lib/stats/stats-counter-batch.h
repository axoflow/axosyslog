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
#ifndef STATS_COUNTER_BATCH_H_INCLUDED
#define STATS_COUNTER_BATCH_H_INCLUDED 1

#include "stats/stats-counter.h"

/*
 * Counter updates done with stats_counter_add_batched() between
 * stats_counter_batch_begin() and stats_counter_batch_end() are collected
 * per thread and applied to the counters at the end of the outermost
 * batch.  Outside of a batch they are applied immediately.
 *
 * Use it for counters that are updated for every message from several
 * threads at once: the atomic increments of a shared counter make its cache
 * line bounce between the CPUs.  The counters are only updated when the
 * batch ends, so they must not be freed in the meantime.
 */
void stats_counter_batch_begin(void);
void stats_counter_batch_end(void);
void _stats_counter_add_batched(StatsCounterItem *counter, gssize add);

static inline void
stats_counter_add_batched(StatsCounterItem *counter, gssize add)
{
  if (counter)
    _stats_counter_add_batched(counter, add);
}

static inline void
stats_counter_inc_batched(StatsCounterItem *counter)
{
  stats_counter_add_batched(counter, 1);
}

#endif
