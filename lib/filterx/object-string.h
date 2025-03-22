/*
 * Copyright (c) 2023 Balazs Scheidler <balazs.scheidler@axoflow.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */
#ifndef FILTERX_OBJECT_STRING_H_INCLUDED
#define FILTERX_OBJECT_STRING_H_INCLUDED

#include "filterx-object.h"
#include "filterx-globals.h"

typedef struct _FilterXString FilterXString;
struct _FilterXString
{
  FilterXObject super;
  const gchar *str;
  guint32 str_len;
  gchar storage[];
};

typedef void (*FilterXStringTranslateFunc)(gchar *target, const gchar *source, gsize source_len);

FILTERX_DECLARE_TYPE(string);
FILTERX_DECLARE_TYPE(bytes);
FILTERX_DECLARE_TYPE(protobuf);

gboolean string_format_json(const gchar *str, gsize str_len, GString *json);
gboolean bytes_format_json(const gchar *str, gsize str_len, GString *json);

const gchar *filterx_bytes_get_value_ref(FilterXObject *s, gsize *length);
const gchar *filterx_protobuf_get_value_ref(FilterXObject *s, gsize *length);
FilterXObject *filterx_typecast_string(FilterXExpr *s, FilterXObject *args[], gsize args_len);
FilterXObject *filterx_typecast_bytes(FilterXExpr *s, FilterXObject *args[], gsize args_len);
FilterXObject *filterx_typecast_protobuf(FilterXExpr *s, FilterXObject *args[], gsize args_len);

FilterXObject *_filterx_string_new(const gchar *str, gssize str_len);
FilterXObject *filterx_string_new_translated(const gchar *str, gssize str_len, FilterXStringTranslateFunc translate);
FilterXObject *filterx_bytes_new(const gchar *str, gssize str_len);
FilterXObject *filterx_protobuf_new(const gchar *str, gssize str_len);

/* NOTE: Consider using filterx_object_extract_string_ref() to also support message_value.
 *
 * Also NOTE: the returned string may not be NUL terminated, although often
 * it will be. Generic code should handle the cases where it is not.
 */
static inline const gchar *
filterx_string_get_value_ref(FilterXObject *s, gsize *length)
{
  FilterXString *self = (FilterXString *) s;

  if (!filterx_object_is_type(s, &FILTERX_TYPE_NAME(string)))
    return NULL;

  if (length)
    *length = self->str_len;
  else
    g_assert(self->str[self->str_len] == 0);
  return self->str;
}

static inline FilterXObject *
filterx_string_new(const gchar *str, gssize str_len)
{
  if (str_len == 0 || str[0] == 0)
    {
      return filterx_object_ref(global_cache.string_cache[FILTERX_STRING_ZERO_LENGTH]);
    }
  else if (str[0] >= '0' && str[0] < '9' && (str_len == 1 || str[1] == 0))
    {
      gint index = str[0] - '0';
      return filterx_object_ref(global_cache.string_cache[FILTERX_STRING_NUMBER0 + index]);
    }
  return _filterx_string_new(str, str_len);
}

void filterx_string_global_init(void);
void filterx_string_global_deinit(void);

#define FILTERX_STRING_STACK_INIT(cstr, cstr_len) \
  { \
    FILTERX_OBJECT_STACK_INIT(string), \
    .str = (cstr), \
    .str_len = (((gssize) cstr_len) == -1 ? (guint32) strlen(cstr) : (guint32) (cstr_len)), \
  }

#define FILTERX_STRING_DECLARE_ON_STACK(_name, cstr, cstr_len) \
  FilterXString __ ## _name ## storage = FILTERX_STRING_STACK_INIT(cstr, cstr_len); \
  FilterXObject *_name = &__ ## _name ## storage .super;

void filterx_string_global_init(void);
void filterx_string_global_deinit(void);

#endif
