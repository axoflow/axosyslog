/*
 * Copyright (c) 2025 Axoflow
 * Copyright (c) 2024 László Várady
 * Copyright (c) 2024 Attila Szakacs
 * Copyright (c) 2025 Balazs Scheidler <balazs.scheidler@axoflow.com>
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
#include "filterx/json-repr.h"
#include "filterx/object-null.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "filterx/object-dict.h"
#include "filterx/object-dict-interface.h"
#include "filterx/object-list.h"
#include "filterx/object-list-interface.h"
#include "filterx/object-extractor.h"
#include "filterx/object-message-value.h"
#include "scratch-buffers.h"
#include "utf8utils.h"

#include "logmsg/type-hinting.h"

/* JSON parsing */

static FilterXObject *
_convert_from_json_array(struct json_object *jso, GError **error)
{
  FilterXObject *res = filterx_list_new();

  filterx_object_cow_wrap(&res);
  for (gsize i = 0; i < json_object_array_length(jso); i++)
    {
      struct json_object *el = json_object_array_get_idx(jso, i);

      FilterXObject *o = filterx_object_from_json_object(el, error);
      if (!o)
        goto error;
      if (!filterx_list_append(res, &o))
        {
          filterx_object_unref(o);
          g_set_error(error, 0, 0, "appending to list failed, index=%ld", i);
          goto error;
        }
      filterx_object_unref(o);
    }
  filterx_object_set_dirty(res, FALSE);
  return res;
error:
  filterx_object_unref(res);
  return NULL;
}

static FilterXObject *
_convert_from_json_object(struct json_object *jso, GError **error)
{
  FilterXObject *res = filterx_dict_new();

  filterx_object_cow_wrap(&res);
  struct json_object_iter itr;
  json_object_object_foreachC(jso, itr)
  {
    FILTERX_STRING_DECLARE_ON_STACK(key, itr.key, -1);
    FilterXObject *o = filterx_object_from_json_object(itr.val, error);
    if (!o)
      goto error;

    if (!filterx_object_set_subscript(res, key, &o))
      {
        filterx_object_unref(o);
        g_set_error(error, 0, 0, "setting dictionary item failed, key=%s", itr.key);
        goto error;
      }
    filterx_object_unref(o);
  }
  filterx_object_set_dirty(res, FALSE);
  return res;
error:
  filterx_object_unref(res);
  return NULL;
}

FilterXObject *
filterx_object_from_json_object(struct json_object *jso, GError **error)
{
  enum json_type jst = json_object_get_type(jso);

  switch (jst)
    {
    case json_type_null:
      return filterx_null_new();
    case json_type_double:
      return filterx_double_new(json_object_get_double(jso));
    case json_type_boolean:
      return filterx_boolean_new(json_object_get_boolean(jso));
    case json_type_int:
      return filterx_integer_new(json_object_get_int64(jso));
    case json_type_string:
      return filterx_string_new(json_object_get_string(jso), json_object_get_string_len(jso));
    case json_type_array:
      return _convert_from_json_array(jso, error);
    case json_type_object:
      return _convert_from_json_object(jso, error);
    default:
      g_assert_not_reached();
    }
  return NULL;
}

FilterXObject *
filterx_object_from_json(const gchar *repr, gssize repr_len, GError **error)
{
  g_return_val_if_fail(error == NULL || (*error) == NULL, NULL);
  struct json_object *jso;
  if (!type_cast_to_json(repr, repr_len, &jso, error))
    return NULL;

  FilterXObject *res = filterx_object_from_json_object(jso, error);
  json_object_put(jso);
  return res;
}

FilterXObject *
filterx_parse_json_call(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  if (!args || args_len != 1)
    {
      filterx_simple_function_argument_error(s, "Incorrect number of arguments", FALSE);
      return NULL;
    }
  FilterXObject *arg = args[0];
  const gchar *repr;
  gsize repr_len;
  if (!filterx_object_extract_string_ref(arg, &repr, &repr_len))
    {
      filterx_simple_function_argument_error(s, "Argument must be a string", FALSE);
      return NULL;
    }
  GError *error = NULL;
  FilterXObject *res = filterx_object_from_json(repr, repr_len, &error);
  if (!res)
    {
      filterx_simple_function_argument_error(s, "Argument must be a parseable JSON string", FALSE);
      return NULL;
    }
  return res;
}

/* JSON formatting */

gboolean
filterx_object_to_json(FilterXObject *value, GString *result)
{
  return filterx_object_format_json_append(value, result);
}

static FilterXObject *
_format_json(FilterXObject *arg)
{
  FilterXObject *result = NULL;
  ScratchBuffersMarker marker;
  GString *result_string = scratch_buffers_alloc_and_mark(&marker);

  if (!filterx_object_to_json(arg, result_string))
    goto exit;

  result = filterx_string_new(result_string->str, result_string->len);

exit:
  scratch_buffers_reclaim_marked(marker);
  return result;
}

FilterXObject *
filterx_format_json_call(FilterXExpr *s, FilterXObject *args[], gsize args_len)
{
  if (!args || args_len != 1)
    {
      filterx_simple_function_argument_error(s, "Incorrect number of arguments", FALSE);
      return NULL;
    }

  FilterXObject *arg = args[0];
  return _format_json(arg);
}
