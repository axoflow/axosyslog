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

#include "filterx/object-list-interface.h"
#include "filterx/object-primitive.h"
#include "filterx/filterx-eval.h"


FilterXObject *
filterx_list_get_subscript(FilterXObject *s, gint64 index)
{
  FILTERX_INTEGER_DECLARE_ON_STACK(index_obj, index);
  FilterXObject *result = filterx_object_get_subscript(s, index_obj);
  filterx_object_unref(index_obj);
  return result;
}

gboolean
filterx_list_set_subscript(FilterXObject *s, gint64 index, FilterXObject **new_value)
{
  FILTERX_INTEGER_DECLARE_ON_STACK(index_obj, index);
  gboolean result = filterx_object_set_subscript(s, index_obj, new_value);

  filterx_object_unref(index_obj);
  return result;
}

gboolean
filterx_list_append(FilterXObject *s, FilterXObject **new_value)
{
  return filterx_object_set_subscript(s, NULL, new_value);
}

gboolean
filterx_list_unset_index(FilterXObject *s, gint64 index)
{
  FILTERX_INTEGER_DECLARE_ON_STACK(index_obj, index);
  gboolean result = filterx_object_unset_key(s, index_obj);

  filterx_object_unref(index_obj);
  return result;
}

gboolean
filterx_list_merge(FilterXObject *s, FilterXObject *other)
{
  other = filterx_ref_unwrap_ro(other);
  g_assert(filterx_object_is_type(other, &FILTERX_TYPE_NAME(list)));

  guint64 len;
  g_assert(filterx_object_len(other, &len));

  for (guint64 i = 0; i < len; i++)
    {
      FilterXObject *value_obj = filterx_list_get_subscript(other, (gint64) MIN(i, G_MAXINT64));
      gboolean success = filterx_list_append(s, &value_obj);

      filterx_object_unref(value_obj);

      if (!success)
        return FALSE;
    }

  return TRUE;
}

static gboolean
_len(FilterXObject *s, guint64 *len)
{
  FilterXList *self = (FilterXList *) s;
  *len = self->len(self);
  return TRUE;
}

static FilterXObject *
_get_subscript(FilterXObject *s, FilterXObject *key)
{
  FilterXList *self = (FilterXList *) s;

  gint64 index;
  if (!filterx_integer_unwrap(key, &index))
    {
      filterx_eval_push_error_info_printf("Failed to get element from list", NULL,
                                          "Index must be integer, got: %s",
                                          filterx_object_get_type_name(key));
      return NULL;
    }

  guint64 normalized_index;
  const gchar *error;
  if (!_normalize_index(self, index, &normalized_index, FALSE, &error))
    {
      filterx_eval_push_error_info_printf("Failed to get element from list", NULL,
                                          "Index out of range: %" G_GINT64_FORMAT ", len: %" G_GUINT64_FORMAT,
                                          index, self->len(self));
      return NULL;
    }

  return self->get_subscript(self, normalized_index);
}

static gboolean
_set_subscript(FilterXObject *s, FilterXObject *key, FilterXObject **new_value)
{
  FilterXList *self = (FilterXList *) s;

  if (!key)
    return self->append(self, new_value);

  gint64 index;
  if (!filterx_integer_unwrap(key, &index))
    {
      filterx_eval_push_error_info_printf("Failed to set element of list", NULL,
                                          "Index must be integer, got: %s",
                                          filterx_object_get_type_name(key));
      return FALSE;
    }

  guint64 normalized_index;
  const gchar *error;
  if (!filterx_list_normalize_index(self->len(self), index, &normalized_index, TRUE, &error))
    {
      filterx_eval_push_error_info_printf("Failed to set element of list", NULL,
                                          "Index out of range: %" G_GINT64_FORMAT ", len: %" G_GUINT64_FORMAT,
                                          index, self->len(self));
      return FALSE;
    }

  return self->set_subscript(self, normalized_index, new_value);
}

static gboolean
_is_key_set(FilterXObject *s, FilterXObject *key)
{
  FilterXList *self = (FilterXList *) s;

  if (!key)
    {
      filterx_eval_push_error_static_info("Failed to check index of list", NULL, "Index must be set");
      return FALSE;
    }

  gint64 index;
  if (!filterx_integer_unwrap(key, &index))
    {
      filterx_eval_push_error_info_printf("Failed to check index of list", NULL,
                                          "Index must be integer, got: %s",
                                          filterx_object_get_type_name(key));
      return FALSE;
    }

  guint64 normalized_index;
  const gchar *error;
  return filterx_list_normalize_index(self->len(self), index, &normalized_index, FALSE, &error);
}

static gboolean
_unset_key(FilterXObject *s, FilterXObject *key)
{
  FilterXList *self = (FilterXList *) s;

  if (!key)
    {
      filterx_eval_push_error_static_info("Failed to unset element of list", NULL, "Index must be set");
      return FALSE;
    }

  gint64 index;
  if (!filterx_integer_unwrap(key, &index))
    {
      filterx_eval_push_error_info_printf("Failed to unset element of list", NULL,
                                          "Index must be integer, got: %s",
                                          filterx_object_get_type_name(key));
      return FALSE;
    }

  guint64 normalized_index;
  const gchar *error;
  if (!filterx_list_normalize_index(self->len(self), index, &normalized_index, FALSE, &error))
    {
      filterx_eval_push_error_info_printf("Failed to unset element of list", NULL,
                                          "%s: %" G_GINT64_FORMAT ", len: %" G_GUINT64_FORMAT,
                                          error, index, self->len(self));
      return FALSE;
    }

  return self->unset_index(self, normalized_index);
}

static gboolean
_format_json(FilterXObject *s, GString *json)
{
  gboolean first = TRUE;

  guint64 list_len;
  if (!filterx_object_len(s, &list_len))
    return FALSE;

  g_string_append_c(json, '[');
  for (guint64 i = 0; i < list_len; i++)
    {
      if (!first)
        g_string_append_c(json, ',');
      else
        first = FALSE;
      FilterXObject *elem = filterx_list_get_subscript(s, i);
      gboolean success = filterx_object_format_json_append(elem, json);
      filterx_object_unref(elem);

      if (!success)
        return FALSE;
    }
  g_string_append_c(json, ']');
  return TRUE;
}

void
filterx_list_init_instance(FilterXList *self, FilterXType *type)
{
  g_assert(type->is_mutable);
  g_assert(type->len == _len);
  g_assert(type->get_subscript == _get_subscript);
  g_assert(type->set_subscript == _set_subscript);
  g_assert(type->is_key_set == _is_key_set);
  g_assert(type->unset_key == _unset_key);

  filterx_object_init_instance(&self->super, type);
}

static FilterXObject *
_add(FilterXObject *lhs_object, FilterXObject *rhs_object)
{
  rhs_object = filterx_ref_unwrap_ro(rhs_object);

  if (!filterx_object_is_type(rhs_object, &FILTERX_TYPE_NAME(list)))
    return NULL;

  FilterXObject *cloned = filterx_object_clone(lhs_object);

  if(!filterx_list_merge(cloned, rhs_object))
    goto error;

  return cloned;
error:
  filterx_object_unref(cloned);
  return NULL;
}

FILTERX_DEFINE_TYPE(list, FILTERX_TYPE_NAME(object),
                    .is_mutable = TRUE,
                    .len = _len,
                    .get_subscript = _get_subscript,
                    .set_subscript = _set_subscript,
                    .is_key_set = _is_key_set,
                    .unset_key = _unset_key,
                    .format_json = _format_json,
                    .add = _add,
                   );
