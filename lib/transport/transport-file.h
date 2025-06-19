/*
 * Copyright (c) 2002-2013 Balabit
 * Copyright (c) 1998-2013 Balázs Scheidler
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

#ifndef TRANSPORT_TRANSPORT_FILE_H_INCLUDED
#define TRANSPORT_TRANSPORT_FILE_H_INCLUDED 1

#include "transport/logtransport.h"

/* log transport that simply sends messages to an fd */
typedef struct _LogTransportFile LogTransportFile;
struct _LogTransportFile
{
  LogTransport super;
};

gssize log_transport_file_read_method(LogTransport *self, gpointer buf, gsize buflen, LogTransportAuxData *aux);
gssize log_transport_file_read_and_ignore_eof_method(LogTransport *self, gpointer buf, gsize buflen,
                                                     LogTransportAuxData *aux);
gssize log_transport_file_write_method(LogTransport *self, const gpointer buf, gsize buflen);

void log_transport_file_init_instance(LogTransportFile *self, gint fd);
LogTransport *log_transport_file_new(gint fd);

#endif
