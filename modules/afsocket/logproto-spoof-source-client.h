/*
 * Copyright (c) 2002-2012 Balabit
 * Copyright (c) 1998-2012 Balázs Scheidler
 * Copyright (c) 2026 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#ifndef LOGPROTO_SPOOF_SOURCE_CLIENT_H_INCLUDED
#define LOGPROTO_SPOOF_SOURCE_CLIENT_H_INCLUDED

#include "logproto/logproto-client.h"
#include "gsockaddr.h"

LogProtoClient *log_proto_spoof_source_client_new(gint address_family,
                                                   const LogProtoClientOptions *options,
                                                   GSockAddr *dest_addr,
                                                   guint max_msglen);

#endif
