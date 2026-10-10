/*
 * Copyright (c) 2026 Adam Kiss
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

#ifndef SPLUNK_S2S_PROTO_SERVER_INCLUDED
#define SPLUNK_S2S_PROTO_SERVER_INCLUDED

#include "logproto/logproto-server.h"
#include "multi-line/multi-line-factory.h"

LogProtoServer *log_proto_splunk_s2s_server_new(LogTransport *transport, const LogProtoServerOptions *options);

/* Merge the lines of raw forwarder chunks into multi-line events, one
 * accumulator per channel.  @multi_line_options must outlive the proto (it is
 * the driver's), a mode of MLM_NONE turns merging off.  @multi_line_timeout
 * is in milliseconds: a pending event is handed over after that much time
 * without a new line, 0 waits for the next line indefinitely.  Call it right
 * after construction, before the first fetch().
 */
void log_proto_splunk_s2s_server_set_multi_line(LogProtoServer *s, const MultiLineOptions *multi_line_options,
                                                gint multi_line_timeout);

/* Act as if the multi-line timer had fired: the next fetch() hands over the
 * pending events whose timeout has passed.  For tests, where no main loop
 * runs the timer. */
void log_proto_splunk_s2s_server_fire_multi_line_timeout(LogProtoServer *s);

#endif
