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

#ifndef COMPAT_GETENT_H_INCLUDED
#define COMPAT_GETENT_H_INCLUDED

#include "compat/compat.h"

#ifndef SYSLOG_NG_HAVE_GETPROTOBYNUMBER_R

#include "getent-generic.h"

#define getprotobynumber_r _compat_generic__getprotobynumber_r
#define getprotobyname_r _compat_generic__getprotobyname_r
#define getservbyport_r _compat_generic__getservbyport_r
#define getservbyname_r _compat_generic__getservbyname_r

#elif defined(__OpenBSD__)

#include "getent-openbsd.h"

#define getprotobynumber_r _compat_openbsd__getprotobynumber_r
#define getprotobyname_r _compat_openbsd__getprotobyname_r
#define getservbyport_r _compat_openbsd__getservbyport_r
#define getservbyname_r _compat_openbsd__getservbyname_r

#elif defined(sun) || defined(__sun)

#include "getent-sun.h"

#define getprotobynumber_r _compat_sun__getprotobynumber_r
#define getprotobyname_r _compat_sun__getprotobyname_r
#define getservbyport_r _compat_sun__getservbyport_r
#define getservbyname_r _compat_sun__getservbyname_r

#endif
#endif
