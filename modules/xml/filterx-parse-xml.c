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

#include "filterx-parse-xml.h"
#include "filterx/object-extractor.h"
#include "filterx/object-string.h"
#include "filterx/filterx-sequence.h"
#include "filterx/filterx-mapping.h"
#include "filterx/filterx-eval.h"
#include "filterx/object-dict.h"
#include "filterx/object-list.h"
#include "scratch-buffers.h"

#include <stdarg.h>

/*
 * Converting XML to a dict is not standardized.  Our intention is to create the most compact dict as possible, which
 * means certain nodes will have different types and structures based on a number of different qualities of the input
 * XML element.  The following points will demonstrate the choices we made in our parser.  In the examples we will use
 * the JSON dict implementation.
 *
 *   1. Empty XML elements become empty strings.
 *
 *        XML:  <foo></foo>
 *        JSON: {"foo": ""}
 *
 *   2. Attributions are stored in @attr key-value pairs, similarly to some other converters (e.g.: python xmltodict).
 *
 *        XML:  <foo bar="123" baz="bad"/>
 *        JSON: {"foo": {"@bar": "123", "@baz": "bad"}}
 *
 *   3. If an XML element has both attributes and a value, we need to store them in a dict, and the value needs a key.
 *      We store the text value under the #text key.
 *
 *        XML:  <foo bar="123">baz</foo>
 *        JSON: {"foo": {"@bar": "123", "#text": "baz"}}
 *
 *   4. An XML element can have both a value and inner elements.  We use the #text key here, too.
 *
 *        XML:  <foo>bar<baz>123</baz></foo>
 *        JSON: {"foo": {"#text": "bar", "baz": "123"}}
 *
 *   5. An XML element can have multiple values separated by inner elements.  In that case we concatenate the values.
 *
 *        XML:  <foo>bar<a></a>baz</foo>
 *        JSON: {"foo": {"#text": "barbaz", "a": ""}}
 *
 * The implementation uses GMarkupParser, which processes the input string from left to right, without any way to
 * look ahead the current element.  GMarkupParser also stores a stack of the XML elements.
 * These result in the following logics:
 *
 *   A. XML elements can initially appear as single nodes but need conversion into lists if additional
 *      elements are encountered at the same level.  This parser stores the initial value, then modify it dynamically
 *      if later elements make it necessary, replacing and moving the value to a dict or list.
 *
 *   B. Although we can access the XML element stack, the resulting dict element stack's depth can be different,
 *      like we see in point 2, where the XML has one element == one depth, but the resulting dict has 2 depth.
 *      Another example is the question of lists, which is simply elements one after another in XML, but a dict has
 *      to create a list beforehand and gather the nodes there.
 *
 *      Because of this depth difference, we cannot use the XML element stack to lookup elements in the resulting dict.
 *      In practice we need to pair the XML element to the respective dict node and we need its parent node, which is
 *      either a list or dict, so we can add/append to it.  XmlElemContext is introduced for this reason.
 */

#define FILTERX_FUNC_PARSE_XML_USAGE "Usage: parse_xml(raw_xml)"
#define XML_ELEM_CTX_STACK_INIT_SIZE (8)

static void _set_error(GError **error, const gchar *format, ...) G_GNUC_PRINTF(2, 0);
static void
_set_error(GError **error, const gchar *format, ...)
{
  if (!error)
    return;

  va_list va;
  va_start(va, format);
  *error = g_error_new_valist(g_quark_from_static_string("filterx-parse-xml"), 0, format, va);
  va_end(va);
}

void
xml_elem_context_set_current_obj(XmlElemContext *self, FilterXObject *current_obj)
{
  filterx_object_unref(self->current_obj);
  self->current_obj = filterx_object_ref(current_obj);
}

void
xml_elem_context_set_parent_obj(XmlElemContext *self, FilterXObject *parent_obj)
{
  filterx_object_unref(self->parent_obj);
  self->parent_obj = filterx_object_ref(parent_obj);
}

void
xml_elem_context_destroy(XmlElemContext *self)
{
  xml_elem_context_set_current_obj(self, NULL);
  xml_elem_context_set_parent_obj(self, NULL);
}

void
xml_elem_context_init(XmlElemContext *self, FilterXObject *parent_obj, FilterXObject *current_obj)
{
  xml_elem_context_set_parent_obj(self, parent_obj);
  xml_elem_context_set_current_obj(self, current_obj);
}


void
filterx_parse_xml_state_init_instance(FilterXParseXmlState *self)
{
  self->xml_elem_context_stack = g_array_sized_new(FALSE, FALSE, sizeof(XmlElemContext), XML_ELEM_CTX_STACK_INIT_SIZE);
  self->free_fn = filterx_parse_xml_state_free_method;
}

void
filterx_parse_xml_state_free_method(FilterXParseXmlState *self)
{
  for (guint i = 0; i < self->xml_elem_context_stack->len; i++)
    xml_elem_context_destroy(&g_array_index(self->xml_elem_context_stack, XmlElemContext, i));
  g_array_free(self->xml_elem_context_stack, TRUE);
}

static FilterXParseXmlState *
_state_new(void)
{
  FilterXParseXmlState *self = g_new0(FilterXParseXmlState, 1);
  filterx_parse_xml_state_init_instance(self);
  return self;
}


static FilterXObject *
_create_object_for_new_elem(FilterXObject *parent_obj, gboolean has_attrs, const gchar **new_elem_repr)
{
  /*
   * If the new element has attributes, we create a dict for them, and we will either set "#text" if this is a leaf
   * or we will create inner dicts otherwise.
   */

  if (has_attrs)
    {
      *new_elem_repr = "{}";
      return filterx_dict_new();
    }

  /*
   * If the new element does not have attributes, we want to see an empty string for it, as it might be a leaf.
   * However, at this point we cannot know whether this elem is a leaf for sure, so there is a logic that converts
   * this empty string to a dict in _convert_to_inner_node() if it is needed.
   */

  *new_elem_repr = "\"\"";
  return filterx_string_new("", 0);
}

static void
_store_first_elem(XmlElemContext *new_elem_context, FilterXObject *new_elem_key,
                  const gchar *new_elem_name, const gchar *new_elem_repr, GError **error)
{
  if (!filterx_object_set_subscript(new_elem_context->parent_obj, new_elem_key, &new_elem_context->current_obj))
    _set_error(error, "failed to store first element: \"%s\"=%s", new_elem_name, new_elem_repr);
}

static gboolean
_is_obj_storing_a_single_elem(FilterXObject *obj)
{
  obj = filterx_ref_unwrap_ro(obj);
  return filterx_object_is_type(obj, &FILTERX_TYPE_NAME(string)) ||
         filterx_object_is_type(obj, &FILTERX_TYPE_NAME(mapping));
}

static void
_store_second_elem(XmlElemContext *new_elem_context, FilterXObject **existing_obj, FilterXObject *new_elem_key,
                   const gchar *new_elem_name, const gchar *new_elem_repr, GError **error)
{
  FilterXObject *list_obj = filterx_list_new();
  if (!list_obj)
    goto fail;

  if (!filterx_sequence_append(list_obj, existing_obj))
    goto fail;

  if (!filterx_sequence_append(list_obj, &new_elem_context->current_obj))
    goto fail;

  if (!filterx_object_set_subscript(new_elem_context->parent_obj, new_elem_key, &list_obj))
    goto fail;

  xml_elem_context_set_parent_obj(new_elem_context, list_obj);
  filterx_object_unref(list_obj);
  return;

fail:
  _set_error(error, "failed to store second element: \"%s\"=[..., %s]", new_elem_name, new_elem_repr);
  filterx_object_unref(list_obj);
}

static gboolean
_is_obj_storing_multiple_elems(FilterXObject *obj)
{
  obj = filterx_ref_unwrap_ro(obj);
  return filterx_object_is_type(obj, &FILTERX_TYPE_NAME(sequence));
}

static void
_store_nth_elem(XmlElemContext *new_elem_context, FilterXObject *existing_obj, FilterXObject *new_elem_key,
                const gchar *new_elem_name, const gchar *new_elem_repr, GError **error)
{
  if (!filterx_sequence_append(existing_obj, &new_elem_context->current_obj))
    {
      _set_error(error, "failed to store nth element: \"%s\"=[..., %s]", new_elem_name, new_elem_repr);
      return;
    }

  xml_elem_context_set_parent_obj(new_elem_context, existing_obj);
}

gboolean
filterx_parse_xml_prepare_elem(const gchar *new_elem_name, XmlElemContext *last_elem_context, gboolean has_attrs,
                               XmlElemContext *new_elem_context, GError **error)
{
  FilterXObject *current_obj = filterx_ref_unwrap_ro(last_elem_context->current_obj);
  g_assert(filterx_object_is_type(current_obj, &FILTERX_TYPE_NAME(mapping)));

  const gchar *new_elem_repr;
  FilterXObject *new_elem_obj = _create_object_for_new_elem(last_elem_context->current_obj, has_attrs, &new_elem_repr);
  xml_elem_context_init(new_elem_context, last_elem_context->current_obj, new_elem_obj);

  FILTERX_STRING_DECLARE_ON_STACK(new_elem_key, new_elem_name, -1);
  FilterXObject *existing_obj = NULL;

  if (!filterx_object_is_key_set(new_elem_context->parent_obj, new_elem_key))
    {
      _store_first_elem(new_elem_context, new_elem_key, new_elem_name, new_elem_repr, error);
      goto exit;
    }

  existing_obj = filterx_object_get_subscript(new_elem_context->parent_obj, new_elem_key);

  if (_is_obj_storing_a_single_elem(existing_obj))
    {
      _store_second_elem(new_elem_context, &existing_obj, new_elem_key, new_elem_name, new_elem_repr, error);
      goto exit;
    }

  if (_is_obj_storing_multiple_elems(existing_obj))
    {
      _store_nth_elem(new_elem_context, existing_obj, new_elem_key, new_elem_name, new_elem_repr, error);
      goto exit;
    }

  msg_debug("FilterX: parse_xml(): Unexpected node type, overwriting",
            evt_tag_str("type", filterx_object_get_type_name(existing_obj)));
  if (!filterx_object_unset_key(new_elem_context->parent_obj, new_elem_key))
    {
      _set_error(error, "failed to unset existing unexpected node");
      goto exit;
    }
  filterx_parse_xml_prepare_elem(new_elem_name, last_elem_context, has_attrs, new_elem_context, error);

exit:
  FILTERX_STRING_CLEAR_FROM_STACK(new_elem_key);
  filterx_object_unref(new_elem_obj);
  filterx_object_unref(existing_obj);

  if (*error)
    {
      xml_elem_context_destroy(new_elem_context);
      return FALSE;
    }

  return TRUE;
}

static void
_collect_attrs(const gchar *element_name, XmlElemContext *elem_context,
               const gchar **attribute_names, const gchar **attribute_values,
               GError **error)
{
  FilterXObject *current_obj = filterx_ref_unwrap_ro(elem_context->current_obj);
  /* Ensured by filterx_parse_xml_prepare_elem() and _create_object_for_new_elem(). */
  g_assert(filterx_object_is_type(current_obj, &FILTERX_TYPE_NAME(mapping)));

  ScratchBuffersMarker marker;
  GString *attr_key = scratch_buffers_alloc_and_mark(&marker);
  g_string_assign(attr_key, "@");

  for (gint i = 0; attribute_names[i]; i++)
    {
      g_string_truncate(attr_key, 1);
      g_string_append(attr_key, attribute_names[i]);

      const gchar *attr_value = attribute_values[i];

      FILTERX_STRING_DECLARE_ON_STACK(key, attr_key->str, attr_key->len);
      FILTERX_STRING_DECLARE_ON_STACK(value, attr_value, -1);

      gboolean success = filterx_object_set_subscript(elem_context->current_obj, key, &value);

      FILTERX_STRING_CLEAR_FROM_STACK(key);
      FILTERX_STRING_CLEAR_FROM_STACK(value);

      if (!success)
        {
          _set_error(error, "failed to store attribute: \"%s\"=\"%s\"", attribute_names[i], attr_value);
          break;
        }
    }

  scratch_buffers_reclaim_marked(marker);
}

static gboolean
_convert_to_dict(GMarkupParseContext *context, XmlElemContext *elem_context, GError **error)
{
  const gchar *parent_elem_name = (const gchar *) g_markup_parse_context_get_element_stack(context)->next->data;
  FILTERX_STRING_DECLARE_ON_STACK(key, parent_elem_name, -1);

  FilterXObject *dict_obj = filterx_dict_new();
  if (!dict_obj)
    goto exit;

  /* current_obj is either a dict or a string, ensured by filterx_parse_xml_prepare_elem() and _text_cb(). */
  gsize existing_value_len;
  const gchar *existing_value;
  g_assert(filterx_object_extract_string_ref(elem_context->current_obj, &existing_value, &existing_value_len));

  if (existing_value_len > 0)
    {
      FILTERX_STRING_DECLARE_ON_STACK(existing_value_key, "#text", 5);
      gboolean success = filterx_object_set_subscript(dict_obj, existing_value_key, &elem_context->current_obj);
      filterx_object_unref(existing_value_key);

      if (!success)
        {
          _set_error(error, "failed to store leaf node value in a new dict: \"%s\"={\"#text\": \"%s\"}",
                     parent_elem_name, existing_value);
          goto exit;
        }
    }

  FilterXObject *parent_obj = filterx_ref_unwrap_rw(elem_context->parent_obj);
  if (filterx_object_is_type(parent_obj, &FILTERX_TYPE_NAME(mapping)))
    {
      if (!filterx_object_set_subscript(parent_obj, key, &dict_obj))
        _set_error(error, "failed to replace leaf node object with: \"%s\"={}", parent_elem_name);
      goto exit;
    }

  if (filterx_object_is_type(parent_obj, &FILTERX_TYPE_NAME(sequence)))
    {
      if (!filterx_sequence_set_subscript(parent_obj, -1, &dict_obj))
        _set_error(error, "failed to replace leaf node object with: {}");
      goto exit;
    }

  g_assert_not_reached();

exit:
  if (!(*error))
    xml_elem_context_set_current_obj(elem_context, dict_obj);

  FILTERX_STRING_CLEAR_FROM_STACK(key);
  filterx_object_unref(dict_obj);
  return !(*error);
}

void
filterx_parse_xml_start_elem_method(FilterXFunctionParseXml *self,
                                    GMarkupParseContext *context, const gchar *element_name,
                                    const gchar **attribute_names, const gchar **attribute_values,
                                    FilterXParseXmlState *state, GError **error)
{
  XmlElemContext *last_elem_context = xml_elem_context_stack_peek_last(state->xml_elem_context_stack);

  FilterXObject *current_obj = filterx_ref_unwrap_ro(last_elem_context->current_obj);
  if (!filterx_object_is_type(current_obj, &FILTERX_TYPE_NAME(mapping)))
    {
      /*
       * We need the last node to be a dict, so we can start a new inner element in it.
       * It can already be a dict, if we already stored an inner element in it,
       * or if it had attributes.
       */
      if (!_convert_to_dict(context, last_elem_context, error))
        return;
    }

  gboolean has_attrs = !!attribute_names[0];

  XmlElemContext new_elem_context = { 0 };
  if (!filterx_parse_xml_prepare_elem(element_name, last_elem_context, has_attrs, &new_elem_context, error))
    return;

  xml_elem_context_stack_push(state->xml_elem_context_stack, &new_elem_context);

  if (has_attrs)
    _collect_attrs(element_name, &new_elem_context, attribute_names, attribute_values, error);
}

void
filterx_parse_xml_end_elem_method(FilterXFunctionParseXml *self,
                                  GMarkupParseContext *context, const gchar *element_name,
                                  FilterXParseXmlState *state, GError **error)
{
  xml_elem_context_stack_remove_last(state->xml_elem_context_stack);
}

static gchar *
_strip(const gchar *text, gsize text_len, gsize *new_text_len)
{
  gchar *stripped_text = g_strndup(text, text_len);
  g_strstrip(stripped_text);

  *new_text_len = strlen(stripped_text);
  if (!(*new_text_len))
    {
      g_free(stripped_text);
      return NULL;
    }

  return stripped_text;
}

void
filterx_parse_xml_replace_string_text(XmlElemContext *elem_context, const gchar *element_name, const gchar *text,
                                      gsize text_len, GError **error)
{
  FILTERX_STRING_DECLARE_ON_STACK(text_obj, text, text_len);

  FilterXObject *parent_obj = filterx_ref_unwrap_rw(elem_context->parent_obj);
  if (filterx_object_is_type(parent_obj, &FILTERX_TYPE_NAME(mapping)))
    {
      FILTERX_STRING_DECLARE_ON_STACK(key, element_name, -1);
      gboolean result = filterx_object_set_subscript(parent_obj, key, &text_obj);
      filterx_object_unref(key);

      if (!result)
        {
          _set_error(error, "failed to add text to dict: \"%s\"=\"%s\"", element_name, text);
          goto fail;
        }
      goto success;
    }

  if (filterx_object_is_type(parent_obj, &FILTERX_TYPE_NAME(sequence)))
    {
      if (!filterx_sequence_set_subscript(parent_obj, -1, &text_obj))
        {
          _set_error(error, "failed to add text to list: \"%s\"", text);
          goto fail;
        }
      goto success;
    }

  g_assert_not_reached();

success:
  xml_elem_context_set_current_obj(elem_context, text_obj);
fail:
  FILTERX_STRING_CLEAR_FROM_STACK(text_obj);
}

static GString *
_concatenate_text_value(FilterXObject *dict, FilterXObject *existing_text_key, const gchar *text, gsize text_len)
{
  GString *buffer = NULL;

  FilterXObject *existing_obj = filterx_object_get_subscript(dict, existing_text_key);
  if (existing_obj)
    {
      gsize existing_value_len;
      const gchar *existing_value;
      if (!filterx_object_extract_string_ref(existing_obj, &existing_value, &existing_value_len))
        {
          msg_debug("FilterX: parse_xml(): Unexpected node type, overwriting",
                    evt_tag_str("type", filterx_object_get_type_name(existing_obj)));
        }
      else if (existing_value_len)
        {
          buffer = scratch_buffers_alloc();
          g_string_append_len(buffer, existing_value, existing_value_len);
          g_string_append_len(buffer, text, text_len);
        }
      filterx_object_unref(existing_obj);
    }

  return buffer;
}

static void
_add_text_to_dict(XmlElemContext *elem_context, const gchar *text, gsize text_len, GError **error)
{
  FILTERX_STRING_DECLARE_ON_STACK(key, "#text", 5);
  GString *new_text = _concatenate_text_value(elem_context->current_obj, key, text, text_len);
  FILTERX_STRING_DECLARE_ON_STACK(text_obj, new_text ? new_text->str : text, new_text ? new_text->len : text_len);

  if (!filterx_object_set_subscript(elem_context->current_obj, key, &text_obj))
    {
      _set_error(error, "failed to add text to dict: \"#text\"=\"%s\"", new_text ? new_text->str : text);
      goto fail;
    }

  xml_elem_context_set_parent_obj(elem_context, elem_context->current_obj);
  xml_elem_context_set_current_obj(elem_context, text_obj);

fail:
  FILTERX_STRING_CLEAR_FROM_STACK(key);
  FILTERX_STRING_CLEAR_FROM_STACK(text_obj);
}

void
filterx_parse_xml_text_method(FilterXFunctionParseXml *self,
                              GMarkupParseContext *context, const gchar *text, gsize text_len,
                              FilterXParseXmlState *state, GError **error)
{
  XmlElemContext *elem_context = xml_elem_context_stack_peek_last(state->xml_elem_context_stack);
  const gchar *element_name = g_markup_parse_context_get_element(context);

  gsize stripped_text_len;
  gchar *stripped_text = _strip(text, text_len, &stripped_text_len);
  if (!stripped_text)
    {
      /*
       * We already stored an empty value in filterx_parse_xml_prepare_elem() and _create_object_for_new_elem().
       * There's nothing to do here.
       */
      return;
    }

  if (filterx_object_is_type(elem_context->current_obj, &FILTERX_TYPE_NAME(string)))
    {
      filterx_parse_xml_replace_string_text(elem_context, element_name, stripped_text, stripped_text_len, error);
      goto exit;
    }

  FilterXObject *current_obj = filterx_ref_unwrap_ro(elem_context->current_obj);
  if (filterx_object_is_type(current_obj, &FILTERX_TYPE_NAME(mapping)))
    {
      _add_text_to_dict(elem_context, stripped_text, stripped_text_len, error);
      goto exit;
    }

  g_assert_not_reached();

exit:
  g_free(stripped_text);
}

static const gchar *
_extract_raw_xml(FilterXFunctionParseXml *self, FilterXObject *xml_obj, gsize *len)
{
  const gchar *raw_xml;
  if (!filterx_object_extract_string_ref(xml_obj, &raw_xml, len))
    {
      filterx_eval_push_error_info_printf("Failed to evaluate parse_xml()", &self->super.super,
                                          "Input must be a string, got: %s",
                                          filterx_object_get_type_name(xml_obj));
      filterx_object_unref(xml_obj);
      return NULL;
    }

  return raw_xml;
}

static void
_start_elem_cb(GMarkupParseContext *context, const gchar *element_name,
               const gchar **attribute_names, const gchar **attribute_values,
               gpointer cb_user_data, GError **error)
{
  FilterXFunctionParseXml *self = ((gpointer *) cb_user_data)[0];
  FilterXParseXmlState *user_data = ((gpointer *) cb_user_data)[1];

  self->start_elem(self, context, element_name, attribute_names, attribute_values, user_data, error);
}

static void
_end_elem_cb(GMarkupParseContext *context, const gchar *element_name,
             gpointer cb_user_data, GError **error)
{
  FilterXFunctionParseXml *self = ((gpointer *) cb_user_data)[0];
  FilterXParseXmlState *user_data = ((gpointer *) cb_user_data)[1];

  self->end_elem(self, context, element_name, user_data, error);
}

static void
_text_cb(GMarkupParseContext *context, const gchar *text, gsize text_len,
         gpointer cb_user_data, GError **error)
{
  FilterXFunctionParseXml *self = ((gpointer *) cb_user_data)[0];
  FilterXParseXmlState *user_data = ((gpointer *) cb_user_data)[1];

  self->text(self, context, text, text_len, user_data, error);
}

static FilterXObject *
_parse(FilterXFunctionParseXml *self, const gchar *raw_xml, gsize raw_xml_len)
{
  static GMarkupParser scanner_callbacks =
  {
    .start_element = _start_elem_cb,
    .end_element = _end_elem_cb,
    .text = _text_cb,
  };

  FilterXObject *result = filterx_dict_new();

  FilterXParseXmlState *state = self->create_state();;
  gpointer user_data[] = { self, state };

  XmlElemContext root_elem_context = { 0 };
  xml_elem_context_init(&root_elem_context, NULL, result);
  xml_elem_context_stack_push(state->xml_elem_context_stack, &root_elem_context);

  GMarkupParseContext *context = g_markup_parse_context_new(&scanner_callbacks, 0, user_data, NULL);

  GError *error = NULL;
  gboolean success = g_markup_parse_context_parse(context, raw_xml, raw_xml_len, &error) &&
                     g_markup_parse_context_end_parse(context, &error);
  if (!success)
    {
      filterx_eval_push_error_info_printf("Failed to evaluate parse_xml()", &self->super.super,
                                          "%s", error ? error->message : "unknown error");
      if (error)
        g_error_free(error);
      filterx_parse_xml_state_free(state);
      g_markup_parse_context_free(context);
      return NULL;
    }

  filterx_parse_xml_state_free(state);
  g_markup_parse_context_free(context);
  return result;
}

static FilterXObject *
_eval(FilterXExpr *s)
{
  FilterXFunctionParseXml *self = (FilterXFunctionParseXml *) s;
  FilterXObject *result = NULL;

  FilterXObject *xml_obj = filterx_expr_eval(self->xml_expr);
  if (!xml_obj)
    {
      filterx_eval_push_error_static_info("Failed to evaluate parse_xml()", &self->super.super,
                                          "Failed to evaluate input expression");
      return NULL;
    }

  gsize raw_xml_len;
  const gchar *raw_xml = _extract_raw_xml(self, xml_obj, &raw_xml_len);
  if (!raw_xml)
    goto exit;

  result = _parse(self, raw_xml, raw_xml_len);

exit:
  filterx_object_unref(xml_obj);
  return result;
}

static gboolean
_extract_args(FilterXFunctionParseXml *self, FilterXFunctionArgs *args, GError **error)
{
  if (filterx_function_args_len(args) != 1)
    {
      g_set_error(error, FILTERX_FUNCTION_ERROR, FILTERX_FUNCTION_ERROR_CTOR_FAIL,
                  "invalid number of arguments. " FILTERX_FUNC_PARSE_XML_USAGE);
      return FALSE;
    }

  self->xml_expr = filterx_function_args_get_expr(args, 0);
  return TRUE;
}

static FilterXExpr *
_optimize(FilterXExpr *s)
{
  FilterXFunctionParseXml *self = (FilterXFunctionParseXml *) s;

  self->xml_expr = filterx_expr_optimize(self->xml_expr);
  return filterx_function_optimize_method(&self->super);
}

static gboolean
_init(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionParseXml *self = (FilterXFunctionParseXml *) s;

  if (!filterx_expr_init(self->xml_expr, cfg))
    return FALSE;

  return filterx_function_init_method(&self->super, cfg);
}

static void
_deinit(FilterXExpr *s, GlobalConfig *cfg)
{
  FilterXFunctionParseXml *self = (FilterXFunctionParseXml *) s;

  filterx_expr_deinit(self->xml_expr, cfg);
  filterx_function_deinit_method(&self->super, cfg);
}

static void
_free(FilterXExpr *s)
{
  FilterXFunctionParseXml *self = (FilterXFunctionParseXml *) s;

  filterx_expr_unref(self->xml_expr);
  filterx_function_free_method(&self->super);
}

FilterXExpr *
filterx_function_parse_xml_new(FilterXFunctionArgs *args, GError **error)
{
  FilterXFunctionParseXml *self = g_new0(FilterXFunctionParseXml, 1);

  filterx_function_init_instance(&self->super, "parse_xml");
  self->super.super.eval = _eval;
  self->super.super.optimize = _optimize;
  self->super.super.init = _init;
  self->super.super.deinit = _deinit;
  self->super.super.free_fn = _free;

  self->create_state = _state_new;
  self->start_elem = filterx_parse_xml_start_elem_method;
  self->end_elem = filterx_parse_xml_end_elem_method;
  self->text = filterx_parse_xml_text_method;

  if (!_extract_args(self, args, error) ||
      !filterx_function_args_check(args, error))
    goto fail;

  filterx_function_args_free(args);
  return &self->super.super;

fail:
  filterx_function_args_free(args);
  filterx_expr_unref(&self->super.super);
  return NULL;
}

FILTERX_FUNCTION(parse_xml, filterx_function_parse_xml_new);
