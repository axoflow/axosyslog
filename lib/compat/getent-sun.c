/*
 * Copyright (c) 2017 Balabit
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

#include "compat/getent-sun.h"

#if defined(sun) || defined(__sun)
#include <errno.h>

int
_compat_sun__getprotobynumber_r(int proto,
                                struct protoent *result_buf, char *buf,
                                size_t buflen, struct protoent **result)
{
  *result = getprotobynumber_r(proto, result_buf, buf, buflen);
  return (*result ? NULL : errno);
}

int
_compat_sun__getprotobyname_r(const char *name,
                              struct protoent *result_buf, char *buf,
                              size_t buflen, struct protoent **result)
{
  *result = getprotobyname_r(name, result_buf, buf, buflen);
  return (*result ? NULL : errno);
}

int
_compat_sun__getservbyport_r(int port, const char *proto,
                             struct servent *result_buf, char *buf,
                             size_t buflen, struct servent **result)
{
  *result =  getservbyport_r(port, proto, result_buf, buf, buflen);
  return (*result ? NULL : errno);
}

int
_compat_sun__getservbyname_r(const char *name, const char *proto,
                             struct servent *result_buf, char *buf,
                             size_t buflen, struct servent **result)
{
  *result =  getservbyname_r(name, proto, result_buf, buf, buflen);
  return (*result ? NULL : errno);
}

#endif
