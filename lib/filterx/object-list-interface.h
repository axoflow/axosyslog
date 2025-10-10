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

#ifndef FILTERX_OBJECT_LIST_INTERFACE_H_INCLUDED
#define FILTERX_OBJECT_LIST_INTERFACE_H_INCLUDED

#include "filterx/filterx-object.h"
#include "filterx/object-primitive.h"

#define FILTERX_LIST_MAX_LENGTH  65536

typedef struct FilterXList_ FilterXList;

struct FilterXList_
{
  FilterXObject super;
};

FilterXObject *filterx_list_get_subscript(FilterXObject *s, gint64 index);
gboolean filterx_list_set_subscript(FilterXObject *s, gint64 index, FilterXObject **new_value);
gboolean filterx_list_append(FilterXObject *s, FilterXObject **new_value);
gboolean filterx_list_unset_index(FilterXObject *s, gint64 index);
gboolean filterx_list_merge(FilterXObject *s, FilterXObject *other);

void filterx_list_init_instance(FilterXList *self, FilterXType *type);

FILTERX_DECLARE_TYPE(list);

static inline gboolean
filterx_list_normalize_index(FilterXObject *index_object,
                             guint64 len,
                             guint64 *normalized_index,
                             gboolean allow_tail,
                             const gchar **error)
{
  gint64 index;

  if (!index_object)
    {
      if (allow_tail)
        {
          *normalized_index = len;
          return TRUE;
        }
      else
        {
          *error = "Index must be set";
          return FALSE;
        }
    }

  if (!filterx_integer_unwrap(index_object, &index))
    {
      *error = "Index must be integer";
      return FALSE;
    }

  if (index >= 0)
    {
      guint64 _index = (guint64) index;
      if (_index > len ||
          (_index == len && !allow_tail))
        {
          *error = "Index out of range";
          return FALSE;
        }
      *normalized_index = _index;
      return TRUE;
    }

  if (len > FILTERX_LIST_MAX_LENGTH)
    {
      *error = "Index exceeds maximal supported value";
      return FALSE;
    }

  if (len + index < 0)
    {
      *error = "Index out of range";
      return FALSE;
    }
  *normalized_index = len + index;

  return TRUE;
}

#endif
