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

#include "filterx/object-dict-interface.h"
#include "filterx/object-extractor.h"
#include "filterx/object-string.h"
#include "filterx/object-list-interface.h"
#include "filterx/filterx-eval.h"
#include "str-utils.h"
#include "utf8utils.h"


gboolean
filterx_dict_iter(FilterXObject *s, FilterXDictIterFunc func, gpointer user_data)
{
  FilterXDict *self = (FilterXDict *) s;
  if (!self->iter)
    return FALSE;
  return self->iter(self, func, user_data);
}

static gboolean
_add_elem_to_dict(FilterXObject *key_obj, FilterXObject *value_obj, gpointer user_data)
{
  FilterXObject *dict = (FilterXObject *) user_data;

  FilterXObject *new_value = filterx_object_ref(value_obj);
  gboolean success = filterx_object_set_subscript(dict, key_obj, &new_value);
  filterx_object_unref(new_value);

  return success;
}

gboolean
filterx_dict_merge(FilterXObject *s, FilterXObject *other)
{
  FilterXDict *self = (FilterXDict *) s;

  other = filterx_ref_unwrap_ro(other);
  g_assert(filterx_object_is_type(other, &FILTERX_TYPE_NAME(dict)));
  return filterx_dict_iter(other, _add_elem_to_dict, self);
}

static gboolean
_add_elem_to_list(FilterXObject *key_obj, FilterXObject *value_obj, gpointer user_data)
{
  FilterXObject *list = (FilterXObject *) user_data;

  FilterXObject *key_to_add = filterx_object_ref(key_obj);
  gboolean success = filterx_list_append(list, &key_to_add);
  filterx_object_unref(key_to_add);

  return success;
}

gboolean
filterx_dict_keys(FilterXObject *s, FilterXObject **keys)
{
  g_assert(s);
  g_assert(keys);
  FilterXObject *obj = filterx_ref_unwrap_ro(s);
  if (!filterx_object_is_type(obj, &FILTERX_TYPE_NAME(dict)))
    goto error;
  g_assert(filterx_object_is_type(*keys, &FILTERX_TYPE_NAME(list)));

  gboolean result = filterx_dict_iter(obj, _add_elem_to_list, *keys);

  filterx_object_unref(s);
  return result;
error:
  filterx_object_unref(s);
  return FALSE;
}

static gboolean
_len(FilterXObject *s, guint64 *len)
{
  FilterXDict *self = (FilterXDict *) s;
  *len = self->len(self);
  return TRUE;
}

static FilterXObject *
_get_subscript(FilterXObject *s, FilterXObject *key)
{
  FilterXDict *self = (FilterXDict *) s;

  if (!key)
    {
      filterx_eval_push_error_static_info("Failed to get element of dict", NULL, "Key is mandatory");
      return NULL;
    }

  return self->get_subscript(self, key);
}

static gboolean
_set_subscript(FilterXObject *s, FilterXObject *key, FilterXObject **new_value)
{
  FilterXDict *self = (FilterXDict *) s;

  if (!key)
    {
      filterx_eval_push_error_static_info("Failed to set element of dict", NULL, "Key is mandatory");
      return FALSE;
    }

  return self->set_subscript(self, key, new_value);
}

static gboolean
_is_key_set(FilterXObject *s, FilterXObject *key)
{
  FilterXDict *self = (FilterXDict *) s;

  if (!key)
    {
      filterx_eval_push_error_static_info("Failed to check key of dict", NULL, "Key is mandatory");
      return FALSE;
    }

  if (self->is_key_set)
    return self->is_key_set(self, key);

  FilterXObject *value = self->get_subscript(self, key);
  filterx_object_unref(value);
  return !!value;
}

static gboolean
_unset_key(FilterXObject *s, FilterXObject *key)
{
  FilterXDict *self = (FilterXDict *) s;

  if (!key)
    {
      filterx_eval_push_error_static_info("Failed to unset element of dict", NULL, "Key is mandatory");
      return FALSE;
    }

  return self->unset_key(self, key);
}

static FilterXObject *
_getattr(FilterXObject *s, FilterXObject *attr)
{
  FilterXDict *self = (FilterXDict *) s;

  if (!self->support_attr)
    return NULL;

  FilterXObject *result = self->get_subscript(self, attr);
  return result;
}

static gboolean
_setattr(FilterXObject *s, FilterXObject *attr, FilterXObject **new_value)
{
  FilterXDict *self = (FilterXDict *) s;

  if (!self->support_attr)
    return FALSE;

  gboolean result = self->set_subscript(self, attr, new_value);
  return result;
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

  if (!filterx_dict_iter(value, _format_and_append_dict_elem, args))
    return FALSE;

  g_string_append_c(json, '}');
  return TRUE;
}

void
filterx_dict_init_instance(FilterXDict *self, FilterXType *type)
{
  g_assert(type->is_mutable);
  g_assert(type->len == _len);
  g_assert(type->get_subscript == _get_subscript);
  g_assert(type->set_subscript == _set_subscript);
  g_assert(type->is_key_set == _is_key_set);
  g_assert(type->unset_key == _unset_key);
  g_assert(type->getattr == _getattr);
  g_assert(type->setattr == _setattr);

  filterx_object_init_instance(&self->super, type);

  self->support_attr = TRUE;
}

static FilterXObject *
_add(FilterXObject *lhs_object, FilterXObject *rhs_object)
{
  rhs_object = filterx_ref_unwrap_ro(rhs_object);
  if (!filterx_object_is_type(rhs_object, &FILTERX_TYPE_NAME(dict)))
    return NULL;

  FilterXObject *cloned = filterx_object_clone(lhs_object);

  if (!filterx_dict_merge(cloned, rhs_object))
    goto error;

  return cloned;
error:
  filterx_object_unref(cloned);
  return NULL;
}

FILTERX_DEFINE_TYPE(dict, FILTERX_TYPE_NAME(object),
                    .is_mutable = TRUE,
                    .len = _len,
                    .get_subscript = _get_subscript,
                    .set_subscript = _set_subscript,
                    .is_key_set = _is_key_set,
                    .unset_key = _unset_key,
                    .getattr = _getattr,
                    .setattr = _setattr,
                    .format_json = _format_json,
                    .add = _add,
                   );
