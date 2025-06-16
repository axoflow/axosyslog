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
#ifndef LOGPROTO_DGRAM_SERVER_H_INCLUDED
#define LOGPROTO_DGRAM_SERVER_H_INCLUDED

#include "logproto-server.h"

/*
 * LogProtoDGramServer
 *
 * This class reads input as datagrams, each datagram is a separate
 * message, regardless of embedded EOL/NUL characters.
 */
LogProtoServer *log_proto_dgram_server_new(LogTransport *transport, const LogProtoServerOptions *options);

#endif
