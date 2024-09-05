/*
 * Copyright (c) 2024 Attila Szakacs
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#include "filterx-format-json.h"
#include "filterx/object-extractor.h"
#include "filterx/object-message-value.h"
#include "filterx/object-string.h"
#include "filterx/object-primitive.h"
#include "filterx/object-null.h"
#include "filterx/object-json.h"
#include "filterx/object-dict-interface.h"
#include "filterx/object-list-interface.h"
#include "scratch-buffers.h"
#include "utf8utils.h"

static gboolean _format_and_append_value(FilterXObject *value, GString *result);

static void
_append_comma_if_needed(GString *result)
{
  if (result->len &&
      result->str[result->len - 1] != '[' &&
      result->str[result->len - 1] != '{' &&
      result->str[result->len - 1] != ':')
    {
      g_string_append_c(result, ',');
    }
}

static gboolean
_append_literal(const gchar *value, gsize len, GString *result)
{
  _append_comma_if_needed(result);
  g_string_append_len(result, value, len);
  return TRUE;
}

static gboolean
_format_and_append_null(GString *result)
{
  _append_comma_if_needed(result);
  g_string_append(result, "null");
  return TRUE;
}

static gboolean
_format_and_append_boolean(gboolean value, GString *result)
{
  _append_comma_if_needed(result);
  g_string_append(result, value ? "true" : "false");
  return TRUE;
}

static gboolean
_format_and_append_integer(gint64 value, GString *result)
{
  _append_comma_if_needed(result);
  g_string_append_printf(result, "%" G_GINT64_FORMAT, value);
  return TRUE;
}

static gboolean
_format_and_append_double(gdouble value, GString *result)
{
  _append_comma_if_needed(result);

  gsize init_len = result->len;
  g_string_set_size(result, init_len + G_ASCII_DTOSTR_BUF_SIZE);
  g_ascii_dtostr(result->str + init_len, G_ASCII_DTOSTR_BUF_SIZE, value);
  g_string_set_size(result, strchr(result->str + init_len, '\0') - result->str);
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
  _append_comma_if_needed(result);
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
  _append_comma_if_needed(result);
  g_string_append_c(result, '"');
  append_unsafe_utf8_as_escaped(result, value, value_len, "\"", "\\u%04x", "\\\\x%02x");
  g_string_append_c(result, '"');
  return TRUE;
}

static gboolean
_format_and_append_dict_elem(FilterXObject *key, FilterXObject *value, gpointer user_data)
{
  GString *result = (GString *) user_data;

  const gchar *key_str;
  gsize key_str_len;
  if (!filterx_object_extract_string(key, &key_str, &key_str_len))
    return FALSE;

  _append_comma_if_needed(result);
  g_string_append_c(result, '"');
  append_unsafe_utf8_as_escaped(result, key_str, key_str_len, "\"", "\\u%04x", "\\\\x%02x");
  g_string_append(result, "\":");

  return _format_and_append_value(value, result);
}

static gboolean
_format_and_append_dict(FilterXObject *value, GString *result)
{
  _append_comma_if_needed(result);
  g_string_append_c(result, '{');

  if (!filterx_dict_iter(value, _format_and_append_dict_elem, (gpointer) result))
    return FALSE;

  g_string_append_c(result, '}');
  return TRUE;
}

static gboolean
_format_and_append_list(FilterXObject *value, GString *result)
{
  _append_comma_if_needed(result);
  g_string_append_c(result, '[');

  guint64 list_len;
  gboolean len_success = filterx_object_len(value, &list_len);
  if (!len_success)
    return FALSE;

  for (guint64 i = 0; i < list_len; i++)
    {
      FilterXObject *elem = filterx_list_get_subscript(value, i);
      gboolean success = _format_and_append_value(elem, result);
      filterx_object_unref(elem);

      if (!success)
        return FALSE;
    }

  g_string_append_c(result, ']');
  return TRUE;
}

static gboolean
_repr_append(FilterXObject *value, GString *result)
{
  ScratchBuffersMarker marker;
  GString *repr = scratch_buffers_alloc_and_mark(&marker);

  gboolean success = filterx_object_repr(value, repr);
  if (!success)
    goto exit;

  _append_comma_if_needed(result);
  g_string_append_c(result, '"');
  append_unsafe_utf8_as_escaped(result, repr->str, repr->len, "\"", "\\u%04x", "\\\\x%02x");
  g_string_append_c(result, '"');

exit:
  scratch_buffers_reclaim_marked(marker);
  return success;
}

static gboolean
_format_and_append_value(FilterXObject *value, GString *result)
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
      _append_comma_if_needed(result);
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

  if (filterx_object_extract_bytes(value, &str, &str_len))
    return _format_and_append_bytes(str, str_len, result);

  if (filterx_object_extract_protobuf(value, &str, &str_len))
    return _format_and_append_protobuf(str, str_len, result);

  if (filterx_object_extract_string(value, &str, &str_len))
    return _format_and_append_string(str, str_len, result);

  if (filterx_object_is_type(value, &FILTERX_TYPE_NAME(dict)))
    return _format_and_append_dict(value, result);

  if (filterx_object_is_type(value, &FILTERX_TYPE_NAME(list)))
    return _format_and_append_list(value, result);

  /* FIXME: handle datetime based on object-datetime.c:_convert_unix_time_to_string() */

  return _repr_append(value, result);
}

static FilterXObject *
_format_json(FilterXObject *arg)
{
  FilterXObject *result = NULL;
  ScratchBuffersMarker marker;
  GString *result_string = scratch_buffers_alloc_and_mark(&marker);

  if (!_format_and_append_value(arg, result_string))
    goto exit;

  result = filterx_string_new(result_string->str, result_string->len);

exit:
  scratch_buffers_reclaim_marked(marker);
  return result;
}

FilterXObject *
filterx_format_json_call(FilterXExpr *s, GPtrArray *args)
{
  if (!args || args->len != 1)
    {
      msg_error("FilterX: format_json(): Invalid number of arguments. "
                "Usage: format_json($data)");
      return NULL;
    }

  FilterXObject *arg = (FilterXObject *) g_ptr_array_index(args, 0);
  return _format_json(arg);
}

FILTERX_SIMPLE_FUNCTION(format_json, filterx_format_json_call);
