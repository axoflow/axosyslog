/*
 * Copyright (c) 2026 Axoflow
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

#ifndef SPLUNK_S2S_OPTIONS_INCLUDED
#define SPLUNK_S2S_OPTIONS_INCLUDED

#include "logproto/logproto-server.h"
#include "multi-line/multi-line-factory.h"

/* The options of transport(splunk-s2s(...)) on a source.  They extend
 * LogProtoServerOptions in place, inside the LogProtoServerOptionsStorage
 * union of the driver's LogReaderOptions, the way LogProtoFileReaderOptions
 * and AltpProtoServerOptions do.  Only the fields added here are ours: the
 * embedded super is defaulted, initialized and destroyed by the LogReader.
 */
typedef struct _SplunkS2SProtoServerOptions
{
  LogProtoServerOptions super;
  MultiLineOptions multi_line_options;
  /* milliseconds, 0 disables the timeout */
  gint multi_line_timeout;
} SplunkS2SProtoServerOptions;

G_STATIC_ASSERT(sizeof(SplunkS2SProtoServerOptions) <= LOG_PROTO_SERVER_OPTIONS_SIZE);

/* Set the splunk-s2s specific part of a LogProtoServerOptionsStorage up.  @s
 * must point to a storage union and not to a bare LogProtoServerOptions. */
SplunkS2SProtoServerOptions *splunk_s2s_proto_server_options_defaults(LogProtoServerOptions *s);

/* The factory constructing the server proto from such options. */
LogProtoServerFactory *splunk_s2s_proto_server_get_factory(void);

#endif
