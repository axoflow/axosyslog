/*
 * Copyright (c) 2002-2018 Balabit
 * Copyright (c) 2018 Laszlo Budai <laszlo.budai@outlook.com>
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

#ifndef CONSECUTIVE_ACK_TRACKER_H_INCLUDED
#define CONSECUTIVE_ACK_TRACKER_H_INCLUDED

#include "ack_tracker.h"

typedef struct _AckTrackerOnAllAcked AckTrackerOnAllAcked;

typedef void (*AckTrackerOnAllAckedFunc)(gpointer);

struct _AckTrackerOnAllAcked
{
  AckTrackerOnAllAckedFunc func;
  gpointer user_data;
  GDestroyNotify user_data_free_fn;
};

gboolean consecutive_ack_tracker_is_empty(AckTracker *self);
void consecutive_ack_tracker_lock(AckTracker *self);
void consecutive_ack_tracker_unlock(AckTracker *self);
void consecutive_ack_tracker_set_on_all_acked(AckTracker *s, AckTrackerOnAllAckedFunc func, gpointer user_data,
                                              GDestroyNotify user_data_free_fn);

AckTracker *consecutive_ack_tracker_new(LogSource *source);

#endif

