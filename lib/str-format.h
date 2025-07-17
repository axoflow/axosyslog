/*
 * Copyright (c) 2002-2011 Balabit
 * Copyright (c) 1998-2011 Balázs Scheidler
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

#ifndef STR_FORMAT_H_INCLUDED
#define STR_FORMAT_H_INCLUDED

#include "syslog-ng.h"

void format_uint32_padded(GString *result, gint field_len, gchar pad_char, gint base, guint32 value);
void format_int32_padded(GString *result, gint field_len, gchar pad_char, gint base, gint32 value);

void format_uint64_padded(GString *result, gint field_len, gchar pad_char, gint base, guint64 value);
void format_int64_padded(GString *result, gint field_len, gchar pad_char, gint base, gint64 value);

gint format_uint64_into_padded_buffer(gchar *result, gsize result_len, gint field_len, gchar pad_char, gint base, guint64 value);
gint format_int64_into_padded_buffer(gchar *result, gsize result_len, gint field_len, gchar pad_char, gint base, gint64 value);

gint format_uint32_into_padded_buffer(gchar *result, gsize result_len, gint field_len, gchar pad_char, gint base, guint32 value);
gint format_int32_into_padded_buffer(gchar *result, gsize result_len, gint field_len, gchar pad_char, gint base, gint32 value);

gchar *format_hex_string(gconstpointer str, gsize str_len, gchar *result, gsize result_len);
gchar *format_hex_string_with_delimiter(gconstpointer str, gsize str_len, gchar *result, gsize result_len,
                                        gchar delimiter);

gboolean scan_positive_int(const gchar **buf, gint *left, gint field_width, gint *num);
gboolean scan_expect_char(const gchar **buf, gint *left, gchar value);
gboolean scan_expect_str(const gchar **buf, gint *left, const gchar *value);

#endif
