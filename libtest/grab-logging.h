/*
 * Copyright (c) 2012-2018 Balabit
 * Copyright (c) 2012 Peter Gyorko
 * Copyright (c) 2012 Balázs Scheidler
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
#ifndef LIBTEST_GRAB_LOGGING_H_INCLUDED
#define LIBTEST_GRAB_LOGGING_H_INCLUDED

#include "syslog-ng.h"

gboolean find_grabbed_message(const gchar *pattern);
void reset_grabbed_messages(void);
void start_grabbing_messages(void);
void stop_grabbing_messages(void);
void display_grabbed_messages(void);
void format_grabbed_messages(GString *log_buffer);
void assert_grabbed_log_contains(const gchar *pattern);

#endif
