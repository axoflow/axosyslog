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

#include "queue_utils_lib.h"
#include "logmsg/logmsg-serialize.h"

#include <stdlib.h>
#include <string.h>
#include <iv.h>
#include <iv_thread.h>
#include <criterion/criterion.h>

int acked_messages = 0;
int fed_messages = 0;

gsize
get_one_message_serialized_size(void)
{
  GString *serialized = g_string_sized_new(256);
  SerializeArchive *sa = serialize_string_archive_new(serialized);
  LogMessage *msg = log_msg_new_empty();

  log_msg_serialize(msg, sa, 0);
  gsize message_length = serialized->len;

  log_msg_unref(msg);
  g_string_free(serialized, TRUE);
  serialize_archive_free(sa);
  return message_length;
}

void
test_ack(LogMessage *msg, AckType ack_type)
{
  acked_messages++;
}

void
feed_empty_messages(LogQueue *q, const LogPathOptions *path_options, gint n)
{
  for (gint i = 0; i < n; i++)
    {
      LogMessage *msg = log_msg_new_empty();

      log_msg_add_ack(msg, path_options);
      msg->ack_func = test_ack;
      log_queue_push_tail(q, msg, path_options);
      fed_messages++;
    }
}

static void
_feed_messages(LogQueue *q, LogPathOptions *path_options, int n)
{
  path_options->ack_needed = TRUE;
  feed_empty_messages(q, path_options, n);
}

void
feed_some_messages(LogQueue *q, int n)
{
  LogPathOptions path_options = LOG_PATH_OPTIONS_INIT;
  path_options.flow_control_requested = TRUE;

  _feed_messages(q, &path_options, n);
}

void
feed_messages_without_flow_control(LogQueue *q, int n)
{
  LogPathOptions path_options = LOG_PATH_OPTIONS_INIT;
  _feed_messages(q, &path_options, n);
}

void
send_some_messages(LogQueue *q, gint n, gboolean remove_from_backlog)
{
  gint i;
  LogPathOptions path_options = LOG_PATH_OPTIONS_INIT;

  for (i = 0; i < n; i++)
    {
      LogMessage *msg = log_queue_pop_head(q, &path_options);
      cr_assert_not_null(msg);

      if (path_options.ack_needed)
        {
          log_msg_ack(msg, &path_options, AT_PROCESSED);
        }
      if (remove_from_backlog)
        log_queue_ack_backlog(q, 1);
      log_msg_unref(msg);
    }
}
