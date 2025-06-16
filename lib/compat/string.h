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
#ifndef COMPAT_STRING_H_INCLUDED
#define COMPAT_STRING_H_INCLUDED

#include "compat.h"
#include <string.h>
#include <stdio.h>

#include <stddef.h>

#ifndef SYSLOG_NG_HAVE_STRTOLL
# if SYSLOG_NG_HAVE_STRTOIMAX || defined(strtoimax)
/* HP-UX has an strtoimax macro, not a function */
#include <inttypes.h>
#define strtoll(nptr, endptr, base) strtoimax(nptr, endptr, base)
# else
/* this requires Glib 2.12 */
#define strtoll(nptr, endptr, base) g_ascii_strtoll(nptr, endptr, base)
# endif
#endif

#ifndef SYSLOG_NG_HAVE_STRCASESTR
char *strcasestr(const char *s, const char *find);
#endif

#ifndef SYSLOG_NG_HAVE_MEMRCHR
void *memrchr(const void *s, int c, size_t n);
#endif

#ifndef SYSLOG_NG_HAVE_STRTOK_R
char *strtok_r(char *string, const char *delim, char **saveptr);
#endif

#ifndef SYSLOG_NG_HAVE_STRNLEN
size_t strnlen(const char *s, size_t maxlen);
#endif

#ifndef SYSLOG_NG_HAVE_GETLINE
ssize_t getline(char **buf, size_t *bufsiz, FILE *fp);
#endif

#endif
