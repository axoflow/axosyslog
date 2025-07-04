/*
 * Copyright (c) 2002-2012 Balabit
 * Copyright (c) 1998-2012 Balázs Scheidler
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

#ifndef LOGPARSER_H_INCLUDED
#define LOGPARSER_H_INCLUDED

#include "logmsg/logmsg.h"
#include "messages.h"
#include "logpipe.h"
#include "template/templates.h"
#include "stats/stats-registry.h"
#include <string.h>

typedef struct _LogParser LogParser;

struct _LogParser
{
  LogPipe super;
  LogTemplate *template_obj;
  StatsCounterItem *processed_messages;
  gboolean (*process)(LogParser *s, LogMessage **pmsg, const LogPathOptions *path_options, const gchar *input,
                      gsize input_len);
  gchar *name;
};

gboolean log_parser_deinit_method(LogPipe *s);
void log_parser_queue_method(LogPipe *s, LogMessage *msg, const LogPathOptions *path_options);
gboolean log_parser_init_method(LogPipe *s);
void log_parser_set_template(LogParser *self, LogTemplate *template_obj);
void log_parser_clone_settings(LogParser *self, LogParser *cloned);
void log_parser_init_instance(LogParser *self, GlobalConfig *cfg);
void log_parser_free_method(LogPipe *self);

static inline gboolean
log_parser_process(LogParser *self, LogMessage **pmsg, const LogPathOptions *path_options, const gchar *input,
                   gssize input_len)
{
  if (input_len < 0)
    input_len = strlen(input);
  return self->process(self, pmsg, path_options, input, input_len);
}

gboolean log_parser_process_message(LogParser *self, LogMessage **pmsg, const LogPathOptions *path_options);

#endif
