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
 */

#ifndef TLSTRANSPORT_H_INCLUDED
#define TLSTRANSPORT_H_INCLUDED

#include "transport/transport-adapter.h"
#include "transport/tls-context.h"

LogTransport *log_transport_tls_new(TLSSession *tls_session, LogTransportIndex base_index);
TLSSession *log_tansport_tls_get_session(LogTransport *s);

void log_transport_tls_global_init(void);
void log_transport_tls_global_deinit(void);

#endif
