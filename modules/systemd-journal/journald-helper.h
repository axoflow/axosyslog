/*
 * Copyright (c) 2010-2014 Balabit
 * Copyright (c) 2010-2014 Viktor Juhasz <viktor.juhasz@balabit.com>
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
#ifndef JOURNALD_HELPER_H_INCLUDED
#define JOURNALD_HELPER_H_INCLUDED

#include "syslog-ng.h"
#include "journald-subsystem.h"

typedef void (*FOREACH_DATA_CALLBACK)(gchar *key, gsize key_len, gchar *value, gsize value_len, gpointer user_data);

void journald_foreach_data(sd_journal *self, FOREACH_DATA_CALLBACK func, gpointer user_data);

void journald_foreach_data(sd_journal *journal, FOREACH_DATA_CALLBACK func, gpointer user_data);
int journald_filter_this_boot(sd_journal *journal);

#endif
