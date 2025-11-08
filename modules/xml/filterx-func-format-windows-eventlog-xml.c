/*
 * Copyright (c) 2025 Axoflow
 * Copyright (c) 2025 Tamas Kosztyu <tamas.kosztyu@axoflow.com>
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
#include "filterx-func-format-windows-eventlog-xml.h"
#include "filterx/filterx-eval.h"
#include "filterx/object-extractor.h"
#include "filterx/filterx-mapping.h"
#include "filterx/filterx-sequence.h"

static gboolean
_append_inner_data_dict_element(FilterXObject *key, FilterXObject *value, gpointer user_data)
{
  FilterXFunctionFormatXML *self = ((gpointer *) user_data)[0];
  GString *buffer = ((gpointer *) user_data)[1];
  const gchar *key_str;
  gsize key_str_len;

  if (!filterx_object_extract_string_ref(key, &key_str, &key_str_len))
    {
      filterx_eval_push_error_info_printf(XML_ERROR_STR, &self->super.super,
                                          "Dict key must be a string, got %s",
                                          filterx_object_get_type_name(key));
      return FALSE;
    }

  const gchar *value_str;
  gsize value_str_len;

  if (!filterx_object_extract_string_ref(value, &value_str, &value_str_len))
    {
      filterx_eval_push_error_info_printf(XML_ERROR_STR, &self->super.super,
                                          "Dict value must be a string, got %s",
                                          filterx_object_get_type_name(value));
      return FALSE;
    }

  if (value_str_len)
    {
      gchar *escaped_value = g_markup_escape_text(value_str, value_str_len);
      g_string_append_printf(buffer, "<Data Name='%s'>%s</Data>", key_str, escaped_value);
      g_free(escaped_value);
    }
  else
    g_string_append_printf(buffer, "<Data Name='%s' />", key_str);
  return TRUE;
}

static gboolean
_append_data_element(FilterXObject *key, FilterXObject *value, gpointer user_data)
{
  FilterXFunctionFormatXML *self = ((gpointer *) user_data)[0];
  GString *buffer = ((gpointer *) user_data)[1];
  const gchar *key_str;
  gsize key_str_len;

  if (!filterx_object_extract_string_ref(key, &key_str, &key_str_len))
    {
      filterx_eval_push_error_info_printf(XML_ERROR_STR, &self->super.super,
                                          "Dict key must be a string, got %s",
                                          filterx_object_get_type_name(key));
      return FALSE;
    }

  const gchar *value_str;
  gsize value_str_len;

  if (!filterx_object_extract_string_ref(value, &value_str, &value_str_len))
    {
      filterx_eval_push_error_info_printf(XML_ERROR_STR, &self->super.super,
                                          "Dict value must be a string, got %s",
                                          filterx_object_get_type_name(value));
      return FALSE;
    }

  gchar *escaped_value = g_markup_escape_text(value_str, value_str_len);
  self->append_leaf(key_str, escaped_value, strlen(escaped_value), buffer);
  g_free (escaped_value);
  return TRUE;
}

static gboolean
_append_data_dict(FilterXObject *key, FilterXObject *value, gpointer user_data)
{
  FilterXObject *value_unwrapped = filterx_ref_unwrap_ro(value);
  if(filterx_object_is_type(value_unwrapped, &FILTERX_TYPE_NAME(mapping)))
    {
      if(!filterx_object_iter(value_unwrapped, _append_inner_data_dict_element, user_data))
        return FALSE;

      return TRUE;
    }
  else if(filterx_object_is_type(value_unwrapped, &FILTERX_TYPE_NAME(sequence)))
    {
      if (!append_list(key, value_unwrapped, user_data))
        return FALSE;

      return TRUE;
    }
  else if(!_append_data_element(key, value, user_data))
    return FALSE;

  return TRUE;
}

static void
_insert_event_id_qualifier(const char *key_str, const char *value_str, GString *buffer)
{
  char *event_id_end_pos = strstr(buffer->str, "EventID") + 7;
  gchar qualifier[1024];

  g_string_overwrite(buffer, (int)(event_id_end_pos - buffer->str), " ");
  gint formatted_size = g_snprintf(qualifier, sizeof(qualifier), "Qualifiers='%s'>", value_str);
  g_string_insert_len(buffer,
                      (int)(event_id_end_pos + 1 - buffer->str), qualifier,
                      MIN(formatted_size, sizeof(qualifier) - 1));
}

static void
_append_leaf(const char *key_str, const char *value_str, gsize value_str_len, GString *buffer)
{
  if (g_strcmp0(key_str, "EventIDQualifiers") == 0)
    _insert_event_id_qualifier(key_str, value_str, buffer);
  else if(value_str_len)
    g_string_append_printf(buffer, "<%s>%s</%s>", key_str, value_str, key_str);
  else
    g_string_append_printf(buffer, "<%s/>", key_str);
}

static gboolean
_append_inner_dict(FilterXObject *key, FilterXObject *dict, gpointer user_data)
{
  GString *buffer = ((gpointer *) user_data)[1];
  gboolean *is_only_attribute_present = ((gpointer *) user_data)[2];
  const gchar *key_str;
  gsize key_str_len;

  if (!filterx_object_extract_string_ref(key, &key_str, &key_str_len))
    {
      FilterXFunctionFormatXML *self = ((gpointer *) user_data)[0];
      filterx_eval_push_error_info_printf(XML_ERROR_STR, &self->super.super,
                                          "Dict key must be a string, got %s",
                                          filterx_object_get_type_name(key));
      return FALSE;
    }

  append_inner_dict_start_tag(key_str, buffer);
  gsize prev_buffer_len = buffer->len;

  if (g_strcmp0(key_str, "EventData") == 0)
    {
      FilterXObject *dict_unwrapped = filterx_ref_unwrap_ro(dict);
      if(!filterx_object_iter(dict_unwrapped, _append_data_dict, user_data))
        return FALSE;
    }
  else
    {
      *is_only_attribute_present = FALSE;
      if (!filterx_object_iter(dict, append_object, user_data))
        return FALSE;
    }

  append_inner_dict_end_tag(key_str, user_data, prev_buffer_len);

  return TRUE;
}

FilterXExpr *
filterx_function_format_windows_eventlog_xml_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXExpr *s = filterx_function_format_xml_new(args, error);
  FilterXFunctionFormatXML *self = (FilterXFunctionFormatXML *)s;

  if (!self)
    return NULL;
  self->append_inner_dict = _append_inner_dict;
  self->append_leaf = _append_leaf;

  return s;
}

FILTERX_FUNCTION(format_windows_eventlog_xml, filterx_function_format_windows_eventlog_xml_new);
