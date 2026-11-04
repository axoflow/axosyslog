/*
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

#ifndef LOGBALANCER_AUTOSCALE_H_INCLUDED
#define LOGBALANCER_AUTOSCALE_H_INCLUDED

#include "logmsg/logmsg.h"
#include "stats/stats-counter.h"

#include <time.h>

/*
 * Rate based mapping of partitions to targets, used by LogBalancer for
 * sticky selection with autoscaling enabled.
 *
 * The rate of each partition (e.g. key) is tracked as an exponentially
 * weighted moving average.  Periodically, targets are assigned to the
 * partitions in proportion to their rate, so a busy partition may use more
 * than one target.  The messages of a partition are spread among its
 * targets based on their receipt time within the second.
 *
 * WFO (workers for orphans) targets are reserved for partitions whose
 * share would round down to zero.
 */

typedef struct _LogBalancerAutoscale LogBalancerAutoscale;

gint log_balancer_autoscale_select(LogBalancerAutoscale *self, const gchar *partition_key, LogMessage *msg);

/* the same as log_balancer_autoscale_select() with an explicit monotonic
 * time, exported for testing */
gint log_balancer_autoscale_select_at(LogBalancerAutoscale *self, const gchar *partition_key, LogMessage *msg,
                                      const struct timespec *now);

void log_balancer_autoscale_set_active_partitions_counter(LogBalancerAutoscale *self, StatsCounterItem *counter);
gint log_balancer_autoscale_get_num_partitions(LogBalancerAutoscale *self);

LogBalancerAutoscale *log_balancer_autoscale_new(gint num_targets, gint wfo);
void log_balancer_autoscale_free(LogBalancerAutoscale *self);

#endif
