/*
 * Copyright (c) 2024 Attila Szakacs
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

#include "filterx/filterx-mapping.h"
#include "filterx/object-extractor.h"
#include "filterx/object-string.h"
#include "filterx/filterx-sequence.h"
#include "filterx/filterx-eval.h"
#include "str-utils.h"
#include "utf8utils.h"


static gboolean
_add_elem(FilterXObject *key_obj, FilterXObject *value_obj, gpointer user_data)
{
  FilterXObject *mapping = (FilterXObject *) user_data;

  FilterXObject *new_value = filterx_object_ref(value_obj);
  gboolean success = filterx_object_set_subscript(mapping, key_obj, &new_value);
  filterx_object_unref(new_value);

  return success;
}

gboolean
filterx_mapping_merge(FilterXObject *s, FilterXObject *other)
{
  other = filterx_ref_unwrap_ro(other);
  g_assert(filterx_object_is_type(other, &FILTERX_TYPE_NAME(mapping)));
  return filterx_object_iter(other, _add_elem, s);
}

static gboolean
_add_elem_to_list(FilterXObject *key_obj, FilterXObject *value_obj, gpointer user_data)
{
  FilterXObject *list = (FilterXObject *) user_data;

  FilterXObject *key_to_add = filterx_object_ref(key_obj);
  gboolean success = filterx_sequence_append(list, &key_to_add);
  filterx_object_unref(key_to_add);

  return success;
}

gboolean
filterx_mapping_keys(FilterXObject *s, FilterXObject **keys)
{
  g_assert(s);
  g_assert(keys);
  FilterXObject *obj = filterx_ref_unwrap_ro(s);
  if (!filterx_object_is_type(obj, &FILTERX_TYPE_NAME(mapping)))
    goto error;
  g_assert(filterx_object_is_type(*keys, &FILTERX_TYPE_NAME(sequence)));

  gboolean result = filterx_object_iter(obj, _add_elem_to_list, *keys);

  filterx_object_unref(s);
  return result;
error:
  filterx_object_unref(s);
  return FALSE;
}

static FilterXObject *
_getattr(FilterXObject *s, FilterXObject *attr)
{
  return filterx_object_get_subscript(s, attr);
}

static gboolean
_setattr(FilterXObject *s, FilterXObject *attr, FilterXObject **new_value)
{
  return filterx_object_set_subscript(s, attr, new_value);
}

static gboolean
_format_and_append_dict_elem(FilterXObject *key, FilterXObject *value, gpointer user_data)
{
  gpointer *args = (gpointer *) user_data;
  GString *json = (GString *) args[0];
  gboolean *first = (gboolean *) args[1];

  const gchar *key_str;
  gsize key_str_len;
  if (!filterx_object_extract_string_ref(key, &key_str, &key_str_len))
    return FALSE;

  if (!(*first))
    g_string_append_c(json, ',');
  else
    *first = FALSE;
  g_string_append_c(json, '"');
  append_unsafe_utf8_as_escaped(json, key_str, key_str_len, AUTF8_UNSAFE_QUOTE, "\\u%04x", "\\\\x%02x");
  g_string_append(json, "\":");

  return filterx_object_format_json_append(value, json);
}

static gboolean
_format_json(FilterXObject *value, GString *json)
{
  gboolean first = TRUE;
  gpointer args[] = { json, &first };

  g_string_append_c(json, '{');

  if (!filterx_object_iter(value, _format_and_append_dict_elem, args))
    return FALSE;

  g_string_append_c(json, '}');
  return TRUE;
}

static FilterXObject *
_add(FilterXObject *lhs_object, FilterXObject *rhs_object)
{
  rhs_object = filterx_ref_unwrap_ro(rhs_object);
  if (!filterx_object_is_type(rhs_object, &FILTERX_TYPE_NAME(mapping)))
    return NULL;

  FilterXObject *cloned = filterx_object_clone(lhs_object);

  if (!filterx_mapping_merge(cloned, rhs_object))
    goto error;

  return cloned;
error:
  filterx_object_unref(cloned);
  return NULL;
}

FILTERX_DEFINE_TYPE(mapping, FILTERX_TYPE_NAME(object),
                    .is_mutable = TRUE,
                    .getattr = _getattr,
                    .setattr = _setattr,
                    .format_json = _format_json,
                    .add = _add,
                   );
