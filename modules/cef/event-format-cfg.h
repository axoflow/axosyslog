/*
 * Copyright (c) 2023 Axoflow
 * Copyright (c) 2024 shifter
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

#ifndef EVENT_FORMAT_CFG_H_INCLUDED
#define EVENT_FORMAT_CFG_H_INCLUDED

#include "filterx/filterx-object.h"

#define EVENT_FORMAT_PARSER_PAIR_SEPARATOR_MAX_LEN (0x05)

typedef struct _FilterXFunctionEventFormatParser FilterXFunctionEventFormatParser;
typedef struct _EventParserContext EventParserContext;
typedef struct _EventFormatterContext EventFormatterContext;

typedef gboolean(*FieldParser)(EventParserContext *ctx, const gchar *value, gint value_len, FilterXObject **result,
                               GError **error, gpointer user_data);

typedef gboolean(*FieldFormatter)(EventFormatterContext *ctx, GString *formatted, FilterXObject *dict);

typedef struct _Field
{
  const gchar *name;
  FieldParser field_parser;
  FieldFormatter field_formatter;
  gboolean optional;
} Field;

typedef struct _Header
{
  const gchar *delimiters;
  size_t num_fields;
  Field *fields;
} Header;

typedef struct _Extensions
{
  gchar value_separator;
  gchar pair_separator[EVENT_FORMAT_PARSER_PAIR_SEPARATOR_MAX_LEN];
} Extensions;

typedef struct _Config
{
  const gchar *signature;
  Header header;
  Extensions extensions;
} Config;

#endif
