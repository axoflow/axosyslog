/*
 * Copyright (c) 2017 Balabit
 * Copyright (c) 2017 Balázs Scheidler
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

#ifndef LOG_PROTO_FILE_READER_H_INCLUDED
#define LOG_PROTO_FILE_READER_H_INCLUDED

#include "logproto/logproto-multiline-server.h"
#include "multi-line/multi-line-factory.h"

typedef struct _LogProtoFileReaderOptions
{
  LogProtoServerOptions super;
  MultiLineOptions multi_line_options;
  gint pad_size;
} LogProtoFileReaderOptions;

LogProtoServer *log_proto_file_reader_new(LogTransport *transport, const LogProtoFileReaderOptions *options);

void log_proto_file_reader_options_defaults(LogProtoFileReaderOptions *options);
gboolean log_proto_file_reader_options_init(LogProtoFileReaderOptions *options, GlobalConfig *cfg);
void log_proto_file_reader_options_destroy(LogProtoFileReaderOptions *options);

#endif
