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
#ifndef HOSTNAME_H_INCLUDED
#define HOSTNAME_H_INCLUDED

#include "syslog-ng.h"


gchar *convert_hostname_to_fqdn(gchar *hostname, gsize hostname_len);
gchar *convert_hostname_to_short_hostname(gchar *hostname, gsize hostname_len);

const gchar *get_local_hostname_fqdn(void);
const gchar *get_local_hostname_short(void);

void hostname_reinit(const gchar *custom_domain);
void hostname_global_init(void);
void hostname_global_deinit(void);

#endif
