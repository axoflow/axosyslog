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

#ifndef FILTERX_OBJECT_DICT_INTERFACE_H_INCLUDED
#define FILTERX_OBJECT_DICT_INTERFACE_H_INCLUDED

#include "filterx/filterx-object.h"

typedef struct FilterXDict_ FilterXDict;

typedef gboolean (*FilterXDictIterFunc)(FilterXObject *, FilterXObject *, gpointer);

struct FilterXDict_
{
  FilterXObject super;
  gboolean support_attr;

  FilterXObject *(*get_subscript)(FilterXDict *s, FilterXObject *key);
  gboolean (*set_subscript)(FilterXDict *s, FilterXObject *key, FilterXObject **new_value);
  gboolean (*is_key_set)(FilterXDict *s, FilterXObject *key);
  gboolean (*unset_key)(FilterXDict *s, FilterXObject *key);
  guint64 (*len)(FilterXDict *s);
  gboolean (*iter)(FilterXDict *s, FilterXDictIterFunc func, gpointer user_data);
};

gboolean filterx_dict_iter(FilterXObject *s, FilterXDictIterFunc func, gpointer user_data);
gboolean filterx_dict_merge(FilterXObject *s, FilterXObject *other);
gboolean filterx_dict_keys(FilterXObject *s, FilterXObject **keys);

void filterx_dict_init_instance(FilterXDict *self, FilterXType *type);

FILTERX_DECLARE_TYPE(dict);

#endif
