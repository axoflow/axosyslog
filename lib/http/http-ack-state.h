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

#ifndef HTTP_ACK_STATE_H_INCLUDED
#define HTTP_ACK_STATE_H_INCLUDED

#include "logproto/logproto-server.h"

/* Shared between a connection and the Bookmarks of the messages it handed
 * to the reader.  A Bookmark may outlive the connection, the pipeline
 * acknowledges at its own pace, so this is reference counted and the
 * connection detaches its wakeup callback when it goes away.
 *
 * The consecutive ack tracker saves the Bookmark of the last message of every
 * newly acknowledged prefix (consecutive_ack_tracker.c), so the durable
 * position is a prefix count of the delivered messages and never decreases.
 * A Bookmark the tracker destroys without saving it, above the durable
 * prefix, belongs to a message whose acknowledgement was an abort.
 */

typedef struct _HTTPAckState HTTPAckState;

HTTPAckState *http_ack_state_new(LogProtoServerWakeupCallback *wakeup);
HTTPAckState *http_ack_state_ref(HTTPAckState *self);
void http_ack_state_unref(HTTPAckState *self);

/* the Bookmark of the message at @position, which is 1 for the first one
 * handed to the reader on the connection; it holds a reference until its ACK */
void http_ack_state_fill_bookmark(HTTPAckState *self, Bookmark *bookmark, guint64 position);

/* arms a wakeup for when @wanted messages are durable, none if they are already */
void http_ack_state_set_wanted(HTTPAckState *self, guint64 wanted);
guint64 http_ack_state_get_durable(HTTPAckState *self);
gboolean http_ack_state_is_aborted(HTTPAckState *self);

/* the connection is going away, its Bookmarks may not */
void http_ack_state_detach(HTTPAckState *self);

#endif
