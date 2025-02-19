/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 Attila Szakacs <attila.szakacs@axoflow.com>
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

#include "filterx-parse-windows-eventlog-xml.h"
#include "filterx/object-string.h"
#include "filterx/object-dict-interface.h"
#include "scratch-buffers.h"

static void _set_error(GError **error, const gchar *format, ...) G_GNUC_PRINTF(2, 0);
static void
_set_error(GError **error, const gchar *format, ...)
{
  if (!error)
    return;

  va_list va;
  va_start(va, format);
  *error = g_error_new_valist(g_quark_from_static_string("filterx-parse-windows-eventlog-xml"), 0, format, va);
  va_end(va);
}


typedef enum FilterXParseWEVTPos_
{
  WEVT_POS_NONE,
  WEVT_POS_EVENT,
  WEVT_POS_EVENT_DATA,
  WEVT_POS_DATA,
} FilterXParseWEVTPos;

typedef struct FilterXParseWEVTState_
{
  FilterXParseXmlState super;
  FilterXParseWEVTPos position;
  gboolean has_named_data;
  GString *last_data_name;
} FilterXParseWEVTState;

static FilterXParseXmlState *
_state_new(void)
{
  FilterXParseWEVTState *self = g_new0(FilterXParseWEVTState, 1);
  filterx_parse_xml_state_init_instance(&self->super);
  self->last_data_name = scratch_buffers_alloc();
  self->position = WEVT_POS_NONE;
  return &self->super;
}

static gboolean
_convert_to_dict(GMarkupParseContext *context, XmlElemContext *elem_context, GError **error)
{
  const gchar *parent_elem_name = (const gchar *) g_markup_parse_context_get_element_stack(context)->next->data;
  FILTERX_STRING_DECLARE_ON_STACK(key, parent_elem_name, -1);

  FilterXObject *dict_obj = filterx_object_create_dict(elem_context->parent_obj);
  if (!dict_obj)
    goto exit;

  FilterXObject *parent_unwrapped = filterx_ref_unwrap_ro(elem_context->parent_obj);
  if (!filterx_object_is_type(parent_unwrapped, &FILTERX_TYPE_NAME(dict)))
    {
      _set_error(error, "failed to convert EventData string to dict, parent must be a dict");
      goto exit;
    }

  if (!filterx_object_set_subscript(elem_context->parent_obj, key, &dict_obj))
    {
      _set_error(error, "failed to replace leaf node object with: \"%s\"={}", parent_elem_name);
      goto exit;
    }

exit:
  if (!(*error))
    xml_elem_context_set_current_obj(elem_context, dict_obj);

  filterx_object_unref(key);
  filterx_object_unref(dict_obj);
  return !(*error);
}

static gboolean
_prepare_elem(const gchar *new_elem_name, XmlElemContext *last_elem_context, XmlElemContext *new_elem_context,
              GError **error)
{
  xml_elem_context_init(new_elem_context, last_elem_context->current_obj, NULL);

  FILTERX_STRING_DECLARE_ON_STACK(new_elem_key, new_elem_name, -1);
  FilterXObject *existing_obj = NULL;

  if (!filterx_object_is_key_set(new_elem_context->parent_obj, new_elem_key))
    {
      FilterXObject *empty_dict = filterx_object_create_dict(new_elem_context->parent_obj);
      xml_elem_context_set_current_obj(new_elem_context, empty_dict);
      filterx_object_unref(empty_dict);

      if (!filterx_object_set_subscript(new_elem_context->parent_obj, new_elem_key, &new_elem_context->current_obj))
        _set_error(error, "failed to prepare dict for named param", new_elem_name);
      goto exit;
    }

  existing_obj = filterx_object_get_subscript(new_elem_context->parent_obj, new_elem_key);
  FilterXObject *existing_obj_unwrapped = filterx_ref_unwrap_ro(existing_obj);
  if (!filterx_object_is_type(existing_obj_unwrapped, &FILTERX_TYPE_NAME(dict)))
    {
      _set_error(error, "failed to prepare dict for named param, parent must be dict, got \"%s\"",
                 existing_obj_unwrapped->type->name);
      goto exit;
    }

  xml_elem_context_set_current_obj(new_elem_context, existing_obj);

exit:
  filterx_object_unref(new_elem_key);
  filterx_object_unref(existing_obj);

  if (*error)
    {
      xml_elem_context_destroy(new_elem_context);
      return FALSE;
    }

  return TRUE;
}

static void
_collect_attrs(const gchar **attribute_names, const gchar **attribute_values,
               FilterXParseWEVTState *state, GError **error)
{
  g_string_assign(state->last_data_name, attribute_values[0]);
  state->has_named_data = TRUE;
}

static gboolean
_has_valid_schema_url(const gchar **attribute_names, const gchar **attribute_values, GError **error)
{
  if (!attribute_names[0])
    return FALSE;

  if (g_strcmp0(attribute_names[0], "xmlns") != 0)
    return FALSE;

  if (g_strcmp0(attribute_values[0], "http://schemas.microsoft.com/win/2004/08/events/event") != 0)
    {
      _set_error(error, "unexpected schema URL: %s", attribute_values[0]);
      return FALSE;
    }

  if (attribute_names[1])
    {
      _set_error(error, "unexpected attribute in Event, number of attributes must be 1, got: %s", attribute_names[1]);
      return FALSE;
    }

  return TRUE;
}

static gboolean
_is_root_elem_valid(const gchar *element_name, const gchar **attribute_names, const gchar **attribute_values,
                    GError **error)
{
  if (g_strcmp0(element_name, "Event") != 0)
    {
      _set_error(error, "unexpected Windows EventLog XML root element: %s, expected \"Event\"", element_name);
      return FALSE;
    }

  if (!_has_valid_schema_url(attribute_names, attribute_values, error))
    return FALSE;

  return TRUE;
}

static gboolean
_push_position(FilterXParseWEVTState *state, const gchar *element_name,
               const gchar **attribute_names, const gchar **attribute_values, GError **error)
{
  switch (state->position)
    {
    case WEVT_POS_NONE:
      if (!_is_root_elem_valid(element_name, attribute_names, attribute_values, error))
        return FALSE;
      state->position = WEVT_POS_EVENT;
      return TRUE;
    case WEVT_POS_EVENT:
      if (g_strcmp0(element_name, "EventData") == 0)
        state->position = WEVT_POS_EVENT_DATA;
      return TRUE;
    case WEVT_POS_EVENT_DATA:
      if (g_strcmp0(element_name, "Data") == 0)
        state->position = WEVT_POS_DATA;
      return TRUE;
    case WEVT_POS_DATA:
      return TRUE;
    default:
      g_assert_not_reached();
    }
}

static void
_pop_position(FilterXParseWEVTState *state, const gchar *element_name)
{
  switch (state->position)
    {
    case WEVT_POS_NONE:
      break;
    case WEVT_POS_EVENT:
      if (g_strcmp0(element_name, "Event") == 0)
        state->position = WEVT_POS_NONE;
      break;
    case WEVT_POS_EVENT_DATA:
      if (g_strcmp0(element_name, "EventData") == 0)
        state->position = WEVT_POS_EVENT;
      break;
    case WEVT_POS_DATA:
      if (g_strcmp0(element_name, "Data") == 0)
        state->position = WEVT_POS_EVENT_DATA;
      break;
    default:
      g_assert_not_reached();
    }
}

static gboolean
_has_wevt_event_data_attr(const gchar **attribute_names, FilterXParseWEVTState *state, GError **error)
{
  if (state->position != WEVT_POS_DATA)
    return FALSE;

  if (!attribute_names[0])
    return FALSE;

  if (g_strcmp0(attribute_names[0], "Name") != 0)
    {
      _set_error(error, "unexpected attribute in Data, expected: Name, got: %s", attribute_names[0]);
      return FALSE;
    }

  if (attribute_names[1])
    {
      _set_error(error, "unexpected attribute in Data, number of attributes must be 1, got: %s", attribute_names[1]);
      return FALSE;
    }

  return TRUE;
}

static void
_start_elem(FilterXGeneratorFunctionParseXml *s,
            GMarkupParseContext *context, const gchar *element_name,
            const gchar **attribute_names, const gchar **attribute_values,
            FilterXParseXmlState *st, GError **error)
{
  FilterXParseWEVTState *state = (FilterXParseWEVTState *) st;
  XmlElemContext *last_elem_context = xml_elem_context_stack_peek_last(state->super.xml_elem_context_stack);

  if (!_push_position(state, element_name, attribute_names, attribute_values, error))
    return;

  if (!_has_wevt_event_data_attr(attribute_names, state, error))
    {
      if (*error)
        return;

      filterx_parse_xml_start_elem_method(s, context, element_name, attribute_names, attribute_values, st, error);
      return;
    }

  FilterXObject *current_obj = filterx_ref_unwrap_ro(last_elem_context->current_obj);
  if (!filterx_object_is_type(current_obj, &FILTERX_TYPE_NAME(dict)))
    {
      if (!_convert_to_dict(context, last_elem_context, error))
        return;
    }

  XmlElemContext new_elem_context = { 0 };
  if (!_prepare_elem(element_name, last_elem_context, &new_elem_context, error))
    return;

  xml_elem_context_stack_push(state->super.xml_elem_context_stack, &new_elem_context);

  _collect_attrs(attribute_names, attribute_values, state, error);
}

static void
_end_elem(FilterXGeneratorFunctionParseXml *s,
          GMarkupParseContext *context, const gchar *element_name,
          FilterXParseXmlState *st, GError **error)
{
  FilterXParseWEVTState *state = (FilterXParseWEVTState *) st;

  _pop_position(state, element_name);
  filterx_parse_xml_end_elem_method(s, context, element_name, st, error);
}

static void
_text(FilterXGeneratorFunctionParseXml *s,
      GMarkupParseContext *context, const gchar *text, gsize text_len,
      FilterXParseXmlState *st, GError **error)
{
  FilterXParseWEVTState *state = (FilterXParseWEVTState *) st;
  XmlElemContext *elem_context = xml_elem_context_stack_peek_last(state->super.xml_elem_context_stack);

  FilterXObject *current_obj = filterx_ref_unwrap_ro(elem_context->current_obj);
  if (!filterx_object_is_type(current_obj, &FILTERX_TYPE_NAME(dict)) ||
      !state->has_named_data)
    {
      filterx_parse_xml_text_method(s, context, text, text_len, st, error);
      return;
    }

  FILTERX_STRING_DECLARE_ON_STACK(key, state->last_data_name->str, state->last_data_name->len);
  FILTERX_STRING_DECLARE_ON_STACK(text_obj, text, text_len);

  if (!filterx_object_set_subscript(elem_context->current_obj, key, &text_obj))
    {
      _set_error(error, "failed to add text to dict: \"%s\"=\"%s\"", state->last_data_name->str, text);
      goto fail;
    }

  xml_elem_context_set_parent_obj(elem_context, elem_context->current_obj);
  xml_elem_context_set_current_obj(elem_context, text_obj);

  state->has_named_data = FALSE;

fail:
  filterx_object_unref(key);
  filterx_object_unref(text_obj);
}

FilterXExpr *
filterx_generator_function_parse_windows_eventlog_xml_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXExpr *s = filterx_generator_function_parse_xml_new(args, error);
  FilterXGeneratorFunctionParseXml *self = (FilterXGeneratorFunctionParseXml *) s;

  if (!self)
    return NULL;

  self->create_state = _state_new;
  self->start_elem = _start_elem;
  self->end_elem = _end_elem;
  self->text = _text;

  return s;
}

FILTERX_GENERATOR_FUNCTION(parse_windows_eventlog_xml, filterx_generator_function_parse_windows_eventlog_xml_new);
