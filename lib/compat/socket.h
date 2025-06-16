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
#ifndef COMPAT_SOCKET_H_INCLUDED
#define COMPAT_SOCKET_H_INCLUDED 1

#include "compat/compat.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>

#ifndef SYSLOG_NG_HAVE_STRUCT_SOCKADDR_STORAGE
struct sockaddr_storage
{
  union
  {
    sa_family_t ss_family;
    struct sockaddr __sa;
    struct sockaddr_un __sun;
    struct sockaddr_in __sin;
#if SYSLOG_NG_ENABLE_IPV6
    struct sockaddr_in6 __sin6;
#endif
  };
};
#endif

#ifndef SYSLOG_NG_HAVE_INET_ATON
int inet_aton(const char *cp, struct in_addr *dst);
#endif

#endif
