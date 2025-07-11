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

#ifndef RELOC_H_INCLUDED
#define RELOC_H_INCLUDED

#include "syslog-ng.h"
#include "cache.h"

void path_resolver_add_configure_variable(CacheResolver *self, const gchar *name, const gchar *value);
CacheResolver *path_resolver_new(const gchar *sysprefix);

gchar *resolve_path_variables_in_text(const gchar *text);
const gchar *get_installation_path_for(const gchar *template);
void override_installation_path_for(const gchar *template, const gchar *value);
void reloc_init(void);
void reloc_deinit(void);

#endif
