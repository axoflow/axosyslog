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

#include "filterx/filterx-sequence.h"
#include "filterx/object-primitive.h"
#include "filterx/filterx-eval.h"


FilterXObject *
filterx_sequence_get_subscript(FilterXObject *s, gint64 index)
{
  FILTERX_INTEGER_DECLARE_ON_STACK(index_obj, index);
  FilterXObject *result = filterx_object_get_subscript(s, index_obj);
  FILTERX_INTEGER_CLEAR_FROM_STACK(index_obj);
  return result;
}

gboolean
filterx_sequence_set_subscript(FilterXObject *s, gint64 index, FilterXObject **new_value)
{
  FILTERX_INTEGER_DECLARE_ON_STACK(index_obj, index);
  gboolean result = filterx_object_set_subscript(s, index_obj, new_value);
  FILTERX_INTEGER_CLEAR_FROM_STACK(index_obj);
  return result;
}

gboolean
filterx_sequence_append(FilterXObject *s, FilterXObject **new_value)
{
  return filterx_object_set_subscript(s, NULL, new_value);
}

gboolean
filterx_sequence_unset(FilterXObject *s, gint64 index)
{
  FILTERX_INTEGER_DECLARE_ON_STACK(index_obj, index);
  gboolean result = filterx_object_unset_key(s, index_obj);
  FILTERX_INTEGER_CLEAR_FROM_STACK(index_obj);

  return result;
}

gboolean
filterx_sequence_merge(FilterXObject *s, FilterXObject *other)
{
  other = filterx_ref_unwrap_ro(other);

  guint64 len;
  if (!filterx_object_len(other, &len))
    return FALSE;

  for (guint64 i = 0; i < len; i++)
    {
      FilterXObject *value_obj = filterx_sequence_get_subscript(other, (gint64) MIN(i, G_MAXINT64));
      gboolean success = filterx_sequence_append(s, &value_obj);

      filterx_object_unref(value_obj);

      if (!success)
        return FALSE;
    }

  return TRUE;
}

static gboolean
_format_and_append_list_elem(FilterXObject *key, FilterXObject *value, gpointer user_data)
{
  gpointer *args = (gpointer *) user_data;
  GString *json = (GString *) args[0];
  gboolean *first = (gboolean *) args[1];

  if (!(*first))
    g_string_append_c(json, ',');
  else
    *first = FALSE;

  if (!filterx_object_format_json_append(value, json))
    return FALSE;
  return TRUE;
}

static gboolean
_format_json(FilterXObject *value, GString *json)
{
  gboolean first = TRUE;
  gpointer args[] = { json, &first };

  g_string_append_c(json, '[');

  if (!filterx_object_iter(value, _format_and_append_list_elem, args))
    return FALSE;

  g_string_append_c(json, ']');
  return TRUE;
}

static FilterXObject *
_add(FilterXObject *lhs_object, FilterXObject *rhs_object)
{
  FilterXObject *cloned = filterx_object_clone(lhs_object);

  if (!filterx_sequence_merge(cloned, rhs_object))
    goto error;

  return cloned;
error:
  filterx_object_unref(cloned);
  return NULL;
}

FILTERX_DEFINE_TYPE(sequence, FILTERX_TYPE_NAME(object),
                    .is_mutable = TRUE,
                    .format_json = _format_json,
                    .add = _add,
                   );
