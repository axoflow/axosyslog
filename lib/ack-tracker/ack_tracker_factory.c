/*
 * Copyright (c) 2020 One Identity
 * Copyright (c) 2020 Laszlo Budai
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

#include "ack_tracker_factory.h"
#include "instant_ack_tracker.h"
#include "consecutive_ack_tracker.h"
#include "ack_tracker.h"

void
ack_tracker_factory_init_instance(AckTrackerFactory *self)
{
  g_atomic_counter_set(&self->ref_cnt, 1);
}

AckTrackerFactory *
ack_tracker_factory_ref(AckTrackerFactory *self)
{
  g_assert(!self || g_atomic_counter_get(&self->ref_cnt) > 0);

  if (self)
    {
      g_atomic_counter_inc(&self->ref_cnt);
    }

  return self;
}

static inline void
_free(AckTrackerFactory *self)
{
  if (self && self->free_fn)
    self->free_fn(self);
}

void
ack_tracker_factory_unref(AckTrackerFactory *self)
{
  g_assert(!self || g_atomic_counter_get(&self->ref_cnt));

  if (self && (g_atomic_counter_dec_and_test(&self->ref_cnt)))
    {
      _free(self);
    }
}
