/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Attila Szakacs-Bertok <attila.szakacs@axoflow.com>
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

#include "http-ack-state.h"

struct _HTTPAckState
{
  GMutex lock;
  gint ref_cnt;
  guint64 durable;
  /* the connection wants a wakeup once @durable reaches @wanted, 0 means it
   * is not waiting */
  guint64 wanted;
  gboolean aborted;
  LogProtoServerWakeupCallback *wakeup;
};

typedef struct _HTTPBookmarkData
{
  HTTPAckState *state;
  guint64 position;
} HTTPBookmarkData;

G_STATIC_ASSERT(sizeof(HTTPBookmarkData) <= sizeof(BookmarkContainer));

HTTPAckState *
http_ack_state_new(LogProtoServerWakeupCallback *wakeup)
{
  HTTPAckState *self = g_new0(HTTPAckState, 1);

  g_mutex_init(&self->lock);
  self->ref_cnt = 1;
  self->wakeup = wakeup;
  return self;
}

HTTPAckState *
http_ack_state_ref(HTTPAckState *self)
{
  g_atomic_int_inc(&self->ref_cnt);
  return self;
}

void
http_ack_state_unref(HTTPAckState *self)
{
  if (g_atomic_int_dec_and_test(&self->ref_cnt))
    {
      g_mutex_clear(&self->lock);
      g_free(self);
    }
}

static void
_wake_locked(HTTPAckState *self)
{
  if (!self->wanted || !self->wakeup)
    return;

  self->wanted = 0;
  log_proto_server_wakeup_cb_call(self->wakeup);
}

/* runs on the destination thread */
static void
_bookmark_save(Bookmark *bookmark)
{
  HTTPBookmarkData *data = (HTTPBookmarkData *) &bookmark->container;
  HTTPAckState *self = data->state;

  g_mutex_lock(&self->lock);
  if (data->position > self->durable)
    self->durable = data->position;

  if (self->durable >= self->wanted)
    _wake_locked(self);
  g_mutex_unlock(&self->lock);
}

static void
_bookmark_release(Bookmark *bookmark)
{
  HTTPBookmarkData *data = (HTTPBookmarkData *) &bookmark->container;

  http_ack_state_unref(data->state);
  data->state = NULL;
  bookmark->save = NULL;
  bookmark->destroy = NULL;
}

static void
_bookmark_destroy(Bookmark *bookmark)
{
  HTTPBookmarkData *data = (HTTPBookmarkData *) &bookmark->container;
  HTTPAckState *self = data->state;

  g_mutex_lock(&self->lock);
  if (data->position > self->durable)
    {
      self->aborted = TRUE;
      _wake_locked(self);
    }
  g_mutex_unlock(&self->lock);

  _bookmark_release(bookmark);
}

void
http_ack_state_fill_bookmark(HTTPAckState *self, Bookmark *bookmark, guint64 position)
{
  HTTPBookmarkData *data = (HTTPBookmarkData *) &bookmark->container;

  /* the ack tracker hands out the same pending Bookmark again when the fetch()
   * it was requested for produced no message, so an earlier fill of ours may
   * still be in there with a reference of its own */
  if (bookmark->destroy == _bookmark_destroy)
    _bookmark_release(bookmark);

  data->state = http_ack_state_ref(self);
  data->position = position;
  bookmark->save = _bookmark_save;
  bookmark->destroy = _bookmark_destroy;
}

/* The test and the arming share one critical section: a _bookmark_save()
 * between two separate steps would have found nothing to wake and the
 * wakeup would never have come. */
void
http_ack_state_set_wanted(HTTPAckState *self, guint64 wanted)
{
  g_mutex_lock(&self->lock);
  self->wanted = self->durable >= wanted ? 0 : wanted;
  g_mutex_unlock(&self->lock);
}

guint64
http_ack_state_get_durable(HTTPAckState *self)
{
  guint64 durable;

  g_mutex_lock(&self->lock);
  durable = self->durable;
  g_mutex_unlock(&self->lock);
  return durable;
}

gboolean
http_ack_state_is_aborted(HTTPAckState *self)
{
  gboolean aborted;

  g_mutex_lock(&self->lock);
  aborted = self->aborted;
  g_mutex_unlock(&self->lock);
  return aborted;
}

void
http_ack_state_detach(HTTPAckState *self)
{
  g_mutex_lock(&self->lock);
  self->wakeup = NULL;
  self->wanted = 0;
  g_mutex_unlock(&self->lock);
}
