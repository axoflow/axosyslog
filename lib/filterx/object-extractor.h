/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 Attila Szakacs <attila.szakacs@axoflow.com>
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
#ifndef FILTERX_OBJECT_EXTRACTOR_H_INCLUDED
#define FILTERX_OBJECT_EXTRACTOR_H_INCLUDED

#include "filterx/filterx-object.h"
#include "filterx/object-message-value.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "filterx/object-datetime.h"
#include "filterx/object-null.h"

#include "generic-number.h"

static inline gboolean
filterx_object_extract_string_ref(FilterXObject *obj, const gchar **value, gsize *len)
{
  if (filterx_object_is_type(obj, &FILTERX_TYPE_NAME(message_value)))
    return filterx_message_value_get_string_ref(obj, value, len);

  *value = filterx_string_get_value_ref(obj, len);
  return !!(*value);
}

static inline gboolean
filterx_object_extract_bytes_ref(FilterXObject *obj, const gchar **value, gsize *len)
{
  if (filterx_object_is_type(obj, &FILTERX_TYPE_NAME(message_value)))
    return filterx_message_value_get_bytes_ref(obj, value, len);

  *value = filterx_bytes_get_value_ref(obj, len);
  return !!(*value);
}

static inline gboolean
filterx_object_extract_protobuf_ref(FilterXObject *obj, const gchar **value, gsize *len)
{
  if (filterx_object_is_type(obj, &FILTERX_TYPE_NAME(message_value)))
    return filterx_message_value_get_protobuf_ref(obj, value, len);

  *value = filterx_protobuf_get_value_ref(obj, len);
  return !!(*value);
}

static inline gboolean
filterx_object_extract_boolean(FilterXObject *obj, gboolean *value)
{
  if (filterx_object_is_type(obj, &FILTERX_TYPE_NAME(message_value)))
    return filterx_message_value_get_boolean(obj, value);

  return filterx_boolean_unwrap(obj, value);
}

static inline gboolean
filterx_object_extract_integer(FilterXObject *obj, gint64 *value)
{
  if (filterx_object_is_type(obj, &FILTERX_TYPE_NAME(message_value)))
    return filterx_message_value_get_integer(obj, value);

  return filterx_integer_unwrap(obj, value);
}

static inline gboolean
filterx_object_extract_double(FilterXObject *obj, gdouble *value)
{
  if (filterx_object_is_type(obj, &FILTERX_TYPE_NAME(message_value)))
    return filterx_message_value_get_double(obj, value);

  return filterx_double_unwrap(obj, value);
}

static inline gboolean
filterx_object_extract_generic_number(FilterXObject *obj, GenericNumber *value)
{
  if (filterx_object_is_type(obj, &FILTERX_TYPE_NAME(primitive)))
    {
      *value = filterx_primitive_get_value(obj);
      return TRUE;
    }

  if (!filterx_object_is_type(obj, &FILTERX_TYPE_NAME(message_value)))
    return FALSE;

  gint64 i;
  if (filterx_message_value_get_integer(obj, &i))
    {
      gn_set_int64(value, i);
      return TRUE;
    }

  gdouble d;
  if (filterx_message_value_get_double(obj, &d))
    {
      gn_set_double(value, d, -1);
      return TRUE;
    }

  gboolean b;
  if (filterx_message_value_get_boolean(obj, &b))
    {
      gn_set_int64(value, (gint64) b);
      return TRUE;
    }

  return FALSE;
}

static inline gboolean
filterx_object_extract_datetime(FilterXObject *obj, UnixTime *value)
{
  if (filterx_object_is_type(obj, &FILTERX_TYPE_NAME(message_value)))
    return filterx_message_value_get_datetime(obj, value);

  if (filterx_object_is_type(obj, &FILTERX_TYPE_NAME(datetime)))
    {
      *value = filterx_datetime_get_value(obj);
      return TRUE;
    }

  return FALSE;
}

static inline gboolean
filterx_object_extract_null(FilterXObject *obj)
{
  if (filterx_object_is_type(obj, &FILTERX_TYPE_NAME(message_value)))
    return filterx_message_value_get_null(obj);

  return filterx_object_is_type(obj, &FILTERX_TYPE_NAME(null));
}

#endif
