/*
 * Copyright (c) 2018 Balabit
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

#ifndef LOGLINE_GENERATOR_H_INCLUDED
#define LOGLINE_GENERATOR_H_INCLUDED

#include "loggen_plugin.h"

int generate_log_line(ThreadData *thread_context,
                      char *buffer, int buffer_length,
                      int syslog_proto,
                      int thread_id,
                      gint64 rate,
                      gsize seq);
int prepare_log_line_template(int syslog_proto, int framing, int message_length, char *sdata_value);

#endif
