/*
 * Copyright (c) 2002-2010 Balabit
 * Copyright (c) 1998-2010 Balázs Scheidler
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

#ifndef LOGMPX_H_INCLUDED
#define LOGMPX_H_INCLUDED

#include "logpipe.h"

/**
 * This class encapsulates a fork of the message pipe-line. It receives
 * messages via its queue() method and forwards them to its list of
 * next_hops in addition to the standard pipe_next next-hop already provided
 * by LogPipe.
 *
 * This object is used for example for each source to send messages to all
 * log pipelines that refer to the source.
 **/
typedef struct _LogMultiplexer
{
  LogPipe super;
  GPtrArray *next_hops;
  gboolean fallback_exists;
  gboolean delivery_propagation;
} LogMultiplexer;

void log_multiplexer_add_next_hop(LogMultiplexer *self, LogPipe *next_hop);
void log_multiplexer_disable_delivery_propagation(LogMultiplexer *self);

LogMultiplexer *log_multiplexer_new(GlobalConfig *cfg);


#endif
