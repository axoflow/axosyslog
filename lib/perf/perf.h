/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 Balázs Scheidler <balazs.scheidler@axoflow.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#ifndef SYSLOG_NG_PERF_H_INCLUDED
#define SYSLOG_NG_PERF_H_INCLUDED

#include "syslog-ng.h"

#if SYSLOG_NG_ENABLE_PERF

gpointer perf_generate_trampoline(gpointer target_address, const gchar *symbol_name);

gboolean perf_is_enabled(void);
gboolean perf_autodetect(void);
gboolean perf_enable(void);

#else

#define perf_generate_trampoline(addr, symbol) ({const gchar *__p G_GNUC_UNUSED = symbol; addr;})
#define perf_is_enabled() FALSE
#define perf_autodetect() FALSE
#define perf_enable() FALSE

#endif

#endif
