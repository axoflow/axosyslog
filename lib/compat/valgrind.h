/*
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
#ifndef SYSLOG_NG_COMPAT_VALGRIND_H_INCLUDED
#define SYSLOG_NG_COMPAT_VALGRIND_H_INCLUDED

#include <syslog-ng.h>

#if SYSLOG_NG_HAVE_VALGRIND_CALLGRIND_H
#include <valgrind/callgrind.h>
#endif

static inline void
valgrind_start_instrumentation(void)
{
#if SYSLOG_NG_HAVE_VALGRIND_CALLGRIND_H
  CALLGRIND_START_INSTRUMENTATION;
#endif
}

static inline void
valgrind_stop_instrumentation(void)
{
#if SYSLOG_NG_HAVE_VALGRIND_CALLGRIND_H
  CALLGRIND_STOP_INSTRUMENTATION;
#endif
}

#endif
