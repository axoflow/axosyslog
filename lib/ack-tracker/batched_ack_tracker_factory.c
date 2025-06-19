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
#include "batched_ack_tracker.h"

typedef struct _BatchedAckTrackerFactory
{
  AckTrackerFactory super;
  struct
  {
    guint timeout;
    guint batch_size;
  } options;
  struct
  {
    BatchedAckTrackerOnBatchAcked func;
    gpointer user_data;
  } on_batch_acked;
} BatchedAckTrackerFactory;


static AckTracker *
_factory_create(AckTrackerFactory *s, LogSource *source)
{
  BatchedAckTrackerFactory *self = (BatchedAckTrackerFactory *) s;

  return batched_ack_tracker_new(source, self->options.timeout, self->options.batch_size,
                                 self->on_batch_acked.func, self->on_batch_acked.user_data);
}

static void
_factory_free(AckTrackerFactory *s)
{
  BatchedAckTrackerFactory *self = (BatchedAckTrackerFactory *)s;

  g_free(self);
}

static void
_init_instance(AckTrackerFactory *s, guint timeout, guint batch_size,
               BatchedAckTrackerOnBatchAcked cb, gpointer user_data)
{
  BatchedAckTrackerFactory *self = (BatchedAckTrackerFactory *) s;
  ack_tracker_factory_init_instance(s);

  self->options.timeout = timeout;
  self->options.batch_size = batch_size;

  self->on_batch_acked.func = cb;
  self->on_batch_acked.user_data = user_data;

  s->create = _factory_create;
  s->free_fn = _factory_free;
  s->type = ACK_BATCHED;
}

AckTrackerFactory *
batched_ack_tracker_factory_new(guint timeout, guint batch_size,
                                BatchedAckTrackerOnBatchAcked cb,
                                gpointer user_data)
{
  BatchedAckTrackerFactory *factory = g_new0(BatchedAckTrackerFactory, 1);
  _init_instance(&factory->super, timeout, batch_size, cb, user_data);

  return &factory->super;
}
