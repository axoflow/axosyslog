/*
 * Copyright (c) 2002-2012 Balabit
 * Copyright (c) 1998-2012 Balázs Scheidler
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
#ifndef STRING_LIST_H_INCLUDED
#define STRING_LIST_H_INCLUDED 1

#include "syslog-ng.h"

GList *string_list_clone(const GList *string_list);
GList *string_array_to_list(const gchar *strlist[]);
GList *string_vargs_to_list_va(const gchar *str, va_list va);
GList *string_vargs_to_list(const gchar *str, ...);
void string_list_free(GList *l);

#endif
