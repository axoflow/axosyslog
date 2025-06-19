/*
 * Copyright (c) 2015 Balabit
 * Copyright (c) 2015 Vincent Bernat <Vincent.Bernat@exoscale.ch>
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

#ifndef DATE_PARSER_H_INCLUDED
#define DATE_PARSER_H_INCLUDED

#include "parser/parser-expr.h"

LogParser *date_parser_new(GlobalConfig *cfg);

void date_parser_set_offset(LogParser *s, goffset offset);
void date_parser_set_formats(LogParser *s, GList *formats);
void date_parser_set_timezone(LogParser *s, gchar *tz);
void date_parser_set_time_stamp(LogParser *s, LogMessageTimeStamp timestamp);
void date_parser_set_value(LogParser *s, const gchar *value_name);
gboolean date_parser_process_flag(LogParser *s, gchar *flag);

#endif
