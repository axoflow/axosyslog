/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 Balázs Scheidler <balazs.scheidler@axoflow.com>
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
