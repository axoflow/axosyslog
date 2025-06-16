/*
 * Copyright (c) 2002-2016 Balabit
 * Copyright (c) 2016 Viktor Juhasz <viktor.juhasz@balabit.com>
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

#ifndef QUEUE_UTILS_LIB_H
#define QUEUE_UTILS_LIB_H 1

#include "logqueue.h"
#include "logpipe.h"

extern int acked_messages;
extern int fed_messages;

void test_ack(LogMessage *msg, AckType ack_type);
void feed_empty_messages(LogQueue *q, const LogPathOptions *path_options, gint n);
void feed_some_messages(LogQueue *q, int n);

void send_some_messages(LogQueue *q, gint n, gboolean remove_from_backlog);

gsize get_one_message_serialized_size(void);
#endif
