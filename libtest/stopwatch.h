/*
 * Copyright (c) 2012 Balabit
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

#ifndef STOPWATCH_H_INCLUDED
#define STOPWATCH_H_INCLUDED 1

#include <glib.h>

void start_stopwatch(void);
guint64 stop_stopwatch_and_get_result(void);
void stop_stopwatch_and_display_result(gint iterations, const gchar *message_template,
                                       ...) G_GNUC_PRINTF(2, 3);

#endif
