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
#include "filterx-func-format-xml.h"
#include "filterx/object-extractor.h"
#include "filterx/filterx-eval.h"
#include "scratch-buffers.h"
#include "filterx/object-dict-interface.h"
#include "filterx/object-list-interface.h"

#define FILTERX_FUNC_FORMAT_XML_USAGE "Usage: format_xml([dict])"
const char *XML_ERROR_STR = "Failed to convert to xml";

static gboolean _append_to_buffer(FilterXObject *key, FilterXObject *value, gpointer user_data);

static gboolean
_append_list(FilterXObject *key, FilterXObject *list, gpointer user_data)
{
  guint64 len;
  g_assert(filterx_object_len(list, &len));

  for (gsize i = 0; i < len; i++)
    {
      FilterXObject *elem = filterx_list_get_subscript(list, i);

      if(!append_object(key, elem, user_data))
        {
          filterx_object_unref(elem);
          return FALSE;
        }
      filterx_object_unref(elem);
    }
  return TRUE;
}

static void
_append_attribute(const char *key_str, const char *value_str, GString *buffer)
{
  if (buffer->str[buffer->len - 1] == '>')
    g_string_overwrite(buffer, buffer->len - 1, " ");
  else
    g_string_append_c(buffer, ' ');

  g_string_append_printf(buffer, "%s='%s'>", &key_str[1], value_str);
}

static void
_append_text(const char *value_str, GString *buffer)
{
  if (buffer->str[buffer->len - 1] != '>')
    g_string_append_c(buffer, '>');

  g_string_append(buffer, value_str);
}

static void
_append_leaf(const char *key_str, const char *value_str, gsize value_str_len, GString *buffer)
{
  if(value_str_len)
    {
      g_string_append_printf(buffer, "<%s>%s</%s>", key_str, value_str, key_str);
    }
  else
    {
      g_string_append_printf(buffer, "<%s/>", key_str);
    }
}

static gboolean
_append_entry(FilterXObject *key, FilterXObject *value, gpointer user_data)
{
  GString *buffer = ((gpointer *) user_data)[1];
  gboolean *is_only_attribute_present = ((gpointer *) user_data)[2];
  const gchar *key_str;
  gsize key_str_len;

  g_assert(filterx_object_extract_string_ref(key, &key_str, &key_str_len));

  GString *val_buf = scratch_buffers_alloc();
  if (!filterx_object_str(value, val_buf))
    {
      FilterXFunctionFormatXML *self = ((gpointer *) user_data)[0];
      filterx_eval_push_error_info_printf(XML_ERROR_STR, &self->super.super,
                                          "Couldn't convert value to string, type: %s",
                                          filterx_object_get_type_name(value));
      return FALSE;
    }

  if (key_str_len && (key_str[0] == '@'))
    {
      *is_only_attribute_present = TRUE;
      _append_attribute(key_str, val_buf->str, buffer);
      return TRUE;
    }
  if (key_str_len && (g_strcmp0(key_str, "#text") == 0))
    {
      *is_only_attribute_present = FALSE;
      _append_text(val_buf->str, buffer);
      return TRUE;
    }

  append_leaf(key_str, val_buf->str, val_buf->len, buffer);
  return TRUE;
}

static gboolean
_append_object(FilterXObject *key, FilterXObject *value, gpointer user_data)
{
  FilterXObject *value_unwrapped = filterx_ref_unwrap_ro(value);

  if (filterx_object_is_type(value_unwrapped, &FILTERX_TYPE_NAME(list)))
    {
      if (!_append_list(key, value_unwrapped, user_data))
        return FALSE;

      return TRUE;
    }

  if(!_append_entry(key, value, user_data))
    return FALSE;

  return TRUE;
}

static FilterXObject *
_eval(FilterXExpr *s)
{
  FilterXFunctionFormatXML *self = (FilterXFunctionFormatXML *) s;

  FilterXObject *input_dict = filterx_expr_eval_typed(self->input);
  if (!input_dict)
    return NULL;

  ScratchBuffersMarker marker;
  GString *formatted = scratch_buffers_alloc_and_mark(&marker);
  FilterXObject *input_dict_unwrapped = filterx_ref_unwrap_ro(input_dict);
  if (!filterx_object_is_type(input_dict_unwrapped, &FILTERX_TYPE_NAME(dict)))
    {
      scratch_buffers_reclaim_marked(marker);
      filterx_object_unref(input_dict);
      filterx_eval_push_error("input must be a dict. " FILTERX_FUNC_FORMAT_XML_USAGE, s, input_dict);
      return NULL;
    }

  gboolean is_only_attribute_present = FALSE;
  gpointer user_data[] = { self, formatted, &is_only_attribute_present };

  if (!filterx_dict_iter(input_dict_unwrapped, append_object, user_data))
    {
      scratch_buffers_reclaim_marked(marker);
      filterx_object_unref(input_dict);
      return NULL;
    }

  scratch_buffers_reclaim_marked(marker);
  filterx_object_unref(input_dict);
  return filterx_string_new(formatted->str, formatted->len);
}

static FilterXExpr *
_optimize(FilterXExpr *s)
{
  FilterXFunctionFormatXML *self = (FilterXFunctionFormatXML *) s;

  self->input = filterx_expr_optimize(self->input);

  return filterx_function_optimize_method(&self->super);
}

static gboolean
_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionFormatXML *self = (FilterXFunctionFormatXML *) s;

  if (!filterx_expr_init(self->input, cfg))
    return FALSE;

  return filterx_function_init_method(&self->super, cfg);
}

static void
_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionFormatXML *self = (FilterXFunctionFormatXML *) s;

  filterx_expr_deinit(self->input, cfg);
  filterx_function_deinit_method(&self->super, cfg);
}

static void
_free(FilterXExpr *s)
{
  FilterXFunctionFormatXML *self = (FilterXFunctionFormatXML *) s;

  filterx_expr_unref(self->input);
  filterx_function_free_method(&self->super);
}

static gboolean
_extract_argument(FilterXFunctionFormatXML *self, FilterXFunctionArgs *args, GError **error)
{
  if (filterx_function_args_len(args) != 1)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. " FILTERX_FUNC_FORMAT_XML_USAGE);
      return FALSE;
    }

  self->input = filterx_function_args_get_expr(args, 0);
  if (!self->input)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "input must be set. " FILTERX_FUNC_FORMAT_XML_USAGE);
      return FALSE;
    }

  return TRUE;
}

FilterXExpr *
filterx_function_format_xml_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionFormatXML *self = g_new0(FilterXFunctionFormatXML, 1);
  filterx_function_init_instance(&self->super, "format_xml");

  self->super.super.eval = _eval;
  self->super.super.optimize = _optimize;
  self->super.super.init = _init;
  self->super.super.deinit = _deinit;
  self->super.super.free_fn = _free;

  if (!_extract_argument(self, args, error) ||
      !filterx_function_args_check(args, error))
    goto error;

  filterx_function_args_free(args);
  return &self->super.super;

error:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}

FILTERX_FUNCTION(format_xml, filterx_function_format_xml_new);