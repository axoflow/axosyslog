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

#ifndef LOGBALANCER_H_INCLUDED
#define LOGBALANCER_H_INCLUDED

#include "logmsg/logmsg.h"
#include "cfg.h"

/*
 * LogBalancer distributes messages between a fixed set of targets (e.g.
 * workers, partitions or connections), identified by their index in the
 * [0, num_targets) range.  It only decides which target a message should
 * go to, delivery is up to the caller.
 *
 * log_balancer_select() is called from source threads.  Round-robin state
 * is kept per input thread, so no locking happens on the hot path.
 */

typedef struct _LogBalancerOptions
{
  /* round-robin: number of consecutive messages sent to the same target */
  gint batch_size;
} LogBalancerOptions;

typedef struct _LogBalancer LogBalancer;

void log_balancer_options_defaults(LogBalancerOptions *options);
gboolean log_balancer_options_init(LogBalancerOptions *options, GlobalConfig *cfg);
void log_balancer_options_destroy(LogBalancerOptions *options);

/* returns a target index in [0, num_targets) */
gint log_balancer_select(LogBalancer *self, LogMessage *msg);
gint log_balancer_get_num_targets(LogBalancer *self);

LogBalancer *log_balancer_new(const LogBalancerOptions *options, gint num_targets);
void log_balancer_free(LogBalancer *self);

#endif
