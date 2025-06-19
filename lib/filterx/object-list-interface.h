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

typedef struct FilterXList_ FilterXList;

struct FilterXList_
{
  FilterXObject super;

  FilterXObject *(*get_subscript)(FilterXList *s, guint64 index);
  gboolean (*set_subscript)(FilterXList *s, guint64 index, FilterXObject **new_value);
  gboolean (*append)(FilterXList *s, FilterXObject **new_value);
  gboolean (*unset_index)(FilterXList *s, guint64 index);
  guint64 (*len)(FilterXList *s);
};

FilterXObject *filterx_list_get_subscript(FilterXObject *s, gint64 index);
gboolean filterx_list_set_subscript(FilterXObject *s, gint64 index, FilterXObject **new_value);
gboolean filterx_list_append(FilterXObject *s, FilterXObject **new_value);
gboolean filterx_list_unset_index(FilterXObject *s, gint64 index);
gboolean filterx_list_merge(FilterXObject *s, FilterXObject *other);

void filterx_list_init_instance(FilterXList *self, FilterXType *type);

FILTERX_DECLARE_TYPE(list);

#endif
