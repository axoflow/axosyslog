/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#include "elasticsearch-bulk-source.h"
#include "http/http-message.h"
#include "socket/socket-options-inet.h"
#include "logmsg/logmsg.h"
#include "msg-format.h"

#include <string.h>

static const gchar *
_next_line(const gchar **data, gsize *remaining, gsize *line_length)
{
  if (!*remaining)
    return NULL;

  const gchar *line = *data;
  const gchar *eol = memchr(line, '\n', *remaining);
  gsize length = eol ? (gsize) (eol - line) : *remaining;

  gsize consumed = eol ? length + 1 : length;
  *data += consumed;
  *remaining -= consumed;

  /* accept both LF and CRLF framing */
  if (length && line[length - 1] == '\r')
    length--;

  *line_length = length;
  return line;
}

static GQueue *
_extract_bulk_pairs(HTTPRequest *http_request, ESBulkSourceDriver *self)
{
  MsgFormatOptions *parse_options = &self->super.super.reader_options.parse_options;

  const GByteArray *body = http_message_get_body(&http_request->super);
  if (!body || !body->data)
    return NULL;

  GQueue *messages = g_queue_new();

  const gchar *data = (const gchar *) body->data;
  gsize remaining = body->len;

  const gchar *action;
  gsize action_length;

  while ((action = _next_line(&data, &remaining, &action_length)))
    {
      /* libbeat separates action/source pairs with a blank line */
      if (action_length == 0)
        continue;

      gsize source_length = 0;
      const gchar *source = _next_line(&data, &remaining, &source_length);

      if (source && source_length)
        {
          LogMessage *msg = msg_format_construct_message(parse_options, (guchar *) source, source_length);
          msg_format_parse_into(parse_options, msg, (guchar *) source, &source_length);
          log_msg_set_value(msg, self->handles.elastic_bulk_action, action, action_length);
          g_queue_push_tail(messages, msg);
        }
    }
  return messages;
}

static GQueue *
_extract_log_messages(HTTPRequest *http_request, HTTPSourceConnection *connection)
{
  ESBulkSourceDriver *self = (ESBulkSourceDriver *) connection->owner;

  return _extract_bulk_pairs(http_request, self);
}

static gboolean
_sd_init(LogPipe *s)
{
  ESBulkSourceDriver *self = (ESBulkSourceDriver *) s;

  self->super.extract_log_messages = _extract_log_messages;

  return http_sd_init_method(s);
}

ESBulkSourceDriver *
elasticsearch_bulk_sd_new(GlobalConfig *cfg)
{
  ESBulkSourceDriver *self = g_new0(ESBulkSourceDriver, 1);
  http_sd_init_instance(&self->super, socket_options_inet_new(), http_transport_mapper_new(), cfg);
  /* the bulk source lines are JSON documents, not syslog messages */
  self->super.super.reader_options.parse_options.flags |= LP_NOPARSE;

  self->handles.elastic_bulk_action = log_msg_get_value_handle(".es_bulk.action");

  self->super.super.super.super.super.init = _sd_init;

  return self;
}
