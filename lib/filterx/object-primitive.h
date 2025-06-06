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
#ifndef FILTERX_PRIMITIVE_H_INCLUDED
#define FILTERX_PRIMITIVE_H_INCLUDED

#include "filterx/filterx-object.h"
#include "generic-number.h"

#define FILTERX_BOOL_CACHE_SIZE 2
extern FilterXObject *fx_bool_cache[FILTERX_BOOL_CACHE_SIZE];

#define FILTERX_INTEGER_CACHE_MIN -128
#define FILTERX_INTEGER_CACHE_MAX 128
#define FILTERX_INTEGER_CACHE_SIZE (FILTERX_INTEGER_CACHE_MAX - FILTERX_INTEGER_CACHE_MIN + 1)
#define FILTERX_INTEGER_CACHE_IDX(v) ((v) - FILTERX_INTEGER_CACHE_MIN)
extern FilterXObject *fx_integer_cache[FILTERX_INTEGER_CACHE_SIZE];

FILTERX_DECLARE_TYPE(primitive);
FILTERX_DECLARE_TYPE(integer);
FILTERX_DECLARE_TYPE(double);
FILTERX_DECLARE_TYPE(boolean);

typedef struct _FilterXPrimitive
{
  FilterXObject super;
  GenericNumber value;
} FilterXPrimitive;

typedef struct _FilterXEnumDefinition
{
  const gchar *name;
  gint64 value;
} FilterXEnumDefinition;

FilterXObject *_filterx_integer_new(gint64 value);
FilterXObject *filterx_double_new(gdouble value);
FilterXObject *filterx_enum_new(GlobalConfig *cfg, const gchar *namespace_name, const gchar *enum_name);
GenericNumber filterx_primitive_get_value(FilterXObject *s);

FilterXObject *filterx_typecast_boolean(FilterXExpr *s, FilterXObject *args[], gsize args_len);
FilterXObject *filterx_typecast_integer(FilterXExpr *s, FilterXObject *args[], gsize args_len);
FilterXObject *filterx_typecast_double(FilterXExpr *s, FilterXObject *args[], gsize args_len);

gboolean bool_repr(gboolean bool_val, GString *repr);
gboolean bool_format_json(gboolean bool_val, GString *json);
gboolean double_repr(gdouble val, GString *repr);
gboolean integer_repr(gint64 val, GString *repr);

/* NOTE: Consider using filterx_object_extract_integer() to also support message_value. */
static inline gboolean
filterx_integer_unwrap(FilterXObject *s, gint64 *value)
{
  FilterXPrimitive *self = (FilterXPrimitive *) s;

  if (!filterx_object_is_type(s, &FILTERX_TYPE_NAME(integer)))
    return FALSE;
  *value = gn_as_int64(&self->value);
  return TRUE;
}

/* NOTE: Consider using filterx_object_extract_double() to also support message_value. */
static inline gboolean
filterx_double_unwrap(FilterXObject *s, gdouble *value)
{
  FilterXPrimitive *self = (FilterXPrimitive *) s;

  if (!filterx_object_is_type(s, &FILTERX_TYPE_NAME(double)))
    return FALSE;
  *value = gn_as_double(&self->value);
  return TRUE;
}

static inline gboolean
filterx_boolean_get_value(FilterXObject *s)
{
  FilterXPrimitive *self = (FilterXPrimitive *) s;

  return !!gn_as_int64(&self->value);
}

/* NOTE: Consider using filterx_object_extract_boolean() to also support message_value. */
static inline gboolean
filterx_boolean_unwrap(FilterXObject *s, gboolean *value)
{
  FilterXPrimitive *self = (FilterXPrimitive *) s;

  if (!filterx_object_is_type(s, &FILTERX_TYPE_NAME(boolean)))
    return FALSE;
  *value = !!gn_as_int64(&self->value);
  return TRUE;
}

static inline FilterXObject *
filterx_boolean_new(gboolean value)
{
  return filterx_object_ref(fx_bool_cache[!!(value)]);
}

static inline FilterXObject *
filterx_integer_new(gint64 value)
{
  if (value >= FILTERX_INTEGER_CACHE_MIN && value <= FILTERX_INTEGER_CACHE_MAX)
    return filterx_object_ref(fx_integer_cache[(FILTERX_INTEGER_CACHE_IDX(value))]);

  return _filterx_integer_new(value);
}

void filterx_primitive_global_init(void);
void filterx_primitive_global_deinit(void);

#define FILTERX_INTEGER_STACK_INIT(v) \
  { \
    FILTERX_OBJECT_STACK_INIT(integer), \
    .value = { \
      { \
        .raw_int64 = v, \
      }, \
      .type = GN_INT64, \
      .precision = 0, \
    }, \
  }

#define FILTERX_INTEGER_DECLARE_ON_STACK(_name, v) \
  FilterXPrimitive __ ## _name ## storage = FILTERX_INTEGER_STACK_INIT(v); \
  FilterXObject *_name = &__ ## _name ## storage .super;

#endif
