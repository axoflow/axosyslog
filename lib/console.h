/*
 * Copyright (c) 2002-2012 Balabit
 * Copyright (c) 1998-2012 Balázs Scheidler
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
#ifndef SYSLOG_NG_CONSOLE_H_INCLUDED
#define SYSLOG_NG_CONSOLE_H_INCLUDED

#include "syslog-ng.h"

void console_printf(const gchar *fmt, ...) __attribute__ ((format (printf, 1, 2)));

gboolean console_is_present(void);
gboolean console_acquire_from_fds(gint fds[3]);
void console_release(void);

void console_global_init(const gchar *console_prefix);
void console_global_deinit(void);

#endif
