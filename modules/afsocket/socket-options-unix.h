/*
 * Copyright (c) 2021 Balazs Scheidler <bazsi77@gmail.com>
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
#ifndef SOCKET_OPTIONS_UNIX_H_INCLUDED
#define SOCKET_OPTIONS_UNIX_H_INCLUDED

#include "socket-options.h"

typedef struct _SocketOptionsUnix
{
  SocketOptions super;
  gint so_passcred;
} SocketOptionsUnix;

static inline void
socket_options_unix_set_so_passcred(SocketOptions *s, gint so_passcred)
{
  SocketOptionsUnix *self = (SocketOptionsUnix *) s;

  self->so_passcred = so_passcred;
}

SocketOptions *socket_options_unix_new(void);

#endif
