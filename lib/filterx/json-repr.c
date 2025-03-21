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
#include "filterx/object-dict-interface.h"
#include "filterx/object-list-interface.h"
#include "filterx/object-json.h"
#include "filterx/object-extractor.h"
#include "filterx/object-message-value.h"
#include "scratch-buffers.h"
#include "utf8utils.h"

#include "logmsg/type-hinting.h"

/* JSON parsing */

static FilterXObject *
_convert_from_json_array(struct json_object *jso, GError **error)
{
  FilterXObject *res = filterx_json_array_new_empty();
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
  return res;
error:
  filterx_object_unref(res);
  return NULL;
}

static FilterXObject *
_convert_from_json_object(struct json_object *jso, GError **error)
{
  FilterXObject *res = filterx_json_object_new_empty();
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

static gboolean
_append_literal(const gchar *value, gsize len, GString *result)
{
  g_string_append_len(result, value, len);
  return TRUE;
}

static gboolean
_format_and_append_null(GString *result)
{
  g_string_append(result, "null");
  return TRUE;
}

static gboolean
_format_and_append_boolean(gboolean value, GString *result)
{
  g_string_append(result, value ? "true" : "false");
  return TRUE;
}

static gboolean
_format_and_append_integer(gint64 value, GString *result)
{
  g_string_append_printf(result, "%" G_GINT64_FORMAT, value);
  return TRUE;
}

static gboolean
_format_and_append_double(gdouble value, GString *result)
{
  double_repr(value, result);
  return TRUE;
}

static inline gsize
_get_base64_encoded_size(gsize len)
{
  return (len / 3 + 1) * 4 + 4;
}

static gboolean
_format_and_append_bytes(const gchar *value, gssize value_len, GString *result)
{
  g_string_append_c(result, '"');

  gint encode_state = 0;
  gint encode_save = 0;
  gsize init_len = result->len;

  /* expand the buffer and add space for the base64 encoded string */
  g_string_set_size(result, init_len + _get_base64_encoded_size(value_len));
  gsize out_len = g_base64_encode_step((const guchar *) value, value_len, FALSE, result->str + init_len,
                                       &encode_state, &encode_save);
  g_string_set_size(result, init_len + out_len + _get_base64_encoded_size(0));

#if !GLIB_CHECK_VERSION(2, 54, 0)
  /* See modules/basicfuncs/str-funcs.c: tf_base64encode() */
  if (((unsigned char *) &encode_save)[0] == 1)
    ((unsigned char *) &encode_save)[2] = 0;
#endif

  out_len += g_base64_encode_close(FALSE, result->str + init_len + out_len, &encode_state, &encode_save);
  g_string_set_size(result, init_len + out_len);

  g_string_append_c(result, '"');
  return TRUE;
}

static gboolean
_format_and_append_protobuf(const gchar *value, gssize value_len, GString *result)
{
  return _format_and_append_bytes(value, value_len, result);
}

static gboolean
_format_and_append_string(const gchar *value, gssize value_len, GString *result)
{
  g_string_append_c(result, '"');
  append_unsafe_utf8_as_escaped(result, value, value_len, AUTF8_UNSAFE_QUOTE, "\\u%04x", "\\\\x%02x");
  g_string_append_c(result, '"');
  return TRUE;
}

static gboolean
_format_and_append_dict_elem(FilterXObject *key, FilterXObject *value, gpointer user_data)
{
  gpointer *args = (gpointer *) user_data;
  GString *result = (GString *) args[0];
  gboolean *first = (gboolean *) args[1];

  const gchar *key_str;
  gsize key_str_len;
  if (!filterx_object_extract_string_ref(key, &key_str, &key_str_len))
    return FALSE;

  if (!(*first))
    g_string_append_c(result, ',');
  else
    *first = FALSE;
  g_string_append_c(result, '"');
  append_unsafe_utf8_as_escaped(result, key_str, key_str_len, AUTF8_UNSAFE_QUOTE, "\\u%04x", "\\\\x%02x");
  g_string_append(result, "\":");

  return filterx_object_to_json(value, result);
}

static gboolean
_format_and_append_dict(FilterXObject *value, GString *result)
{
  gboolean first = TRUE;
  gpointer args[] = { result, &first };

  g_string_append_c(result, '{');

  if (!filterx_dict_iter(value, _format_and_append_dict_elem, args))
    return FALSE;

  g_string_append_c(result, '}');
  return TRUE;
}

static gboolean
_format_and_append_list(FilterXObject *value, GString *result)
{
  gboolean first = TRUE;
  g_string_append_c(result, '[');

  guint64 list_len;
  gboolean len_success = filterx_object_len(value, &list_len);
  if (!len_success)
    return FALSE;

  for (guint64 i = 0; i < list_len; i++)
    {
      if (!first)
        g_string_append_c(result, ',');
      else
        first = FALSE;
      FilterXObject *elem = filterx_list_get_subscript(value, i);
      gboolean success = filterx_object_to_json(elem, result);
      filterx_object_unref(elem);

      if (!success)
        return FALSE;
    }

  g_string_append_c(result, ']');
  return TRUE;
}

static gboolean
_json_append(FilterXObject *value, GString *result)
{
  struct json_object *jso;
  FilterXObject *assoc_object = NULL;
  gboolean success = filterx_object_map_to_json(value, &jso, &assoc_object);
  if (!success)
    goto exit;

  const gchar *json = json_object_to_json_string_ext(jso, JSON_C_TO_STRING_PLAIN | JSON_C_TO_STRING_NOSLASHESCAPE);
  g_string_append(result, json);

exit:
  filterx_object_unref(assoc_object);
  json_object_put(jso);
  return success;
}

gboolean
filterx_object_to_json(FilterXObject *value, GString *result)
{
  if (filterx_object_is_type(value, &FILTERX_TYPE_NAME(message_value)) &&
      (filterx_message_value_get_type(value) == LM_VT_JSON ||
       filterx_message_value_get_type(value) == LM_VT_INTEGER ||
       filterx_message_value_get_type(value) == LM_VT_DOUBLE))
    {
      gsize len;
      const gchar *str = filterx_message_value_get_value(value, &len);
      return _append_literal(str, len, result);
    }

  const gchar *json_literal = filterx_json_to_json_literal(value);
  if (json_literal)
    {
      g_string_append(result, json_literal);
      return TRUE;
    }

  if (filterx_object_extract_null(value))
    return _format_and_append_null(result);

  gboolean b;
  if (filterx_object_extract_boolean(value, &b))
    return _format_and_append_boolean(b, result);

  gint64 i;
  if (filterx_object_extract_integer(value, &i))
    return _format_and_append_integer(i, result);

  gdouble d;
  if (filterx_object_extract_double(value, &d))
    return _format_and_append_double(d, result);

  const gchar *str;
  gsize str_len;

  if (filterx_object_extract_bytes_ref(value, &str, &str_len))
    return _format_and_append_bytes(str, str_len, result);

  if (filterx_object_extract_protobuf_ref(value, &str, &str_len))
    return _format_and_append_protobuf(str, str_len, result);

  if (filterx_object_extract_string_ref(value, &str, &str_len))
    return _format_and_append_string(str, str_len, result);

  FilterXObject *value_unwrapped = filterx_ref_unwrap_ro(value);
  if (filterx_object_is_type(value_unwrapped, &FILTERX_TYPE_NAME(dict)))
    return _format_and_append_dict(value_unwrapped, result);

  if (filterx_object_is_type(value_unwrapped, &FILTERX_TYPE_NAME(list)))
    return _format_and_append_list(value_unwrapped, result);

  return _json_append(value, result);
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
