/*
 * Copyright (c) 2002-2013 Balabit
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
#include "compat/socket.h"

#ifndef SYSLOG_NG_HAVE_INET_ATON
int
inet_aton(const char *cp, struct in_addr *dst)
{
  in_addr_t s = 0;

  s = inet_addr(cp);
  if (s == INADDR_NONE)
    return 0;

  dst->s_addr = s;
  return 1;
}
#endif
