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
#include "filterx/object-string.h"
#include "filterx/object-message-value.h"
#include "filterx/object-extractor.h"

typedef struct FilterXMapping_ FilterXMapping;

struct FilterXMapping_
{
  FilterXObject super;
};

gboolean filterx_mapping_merge(FilterXObject *s, FilterXObject *other);
gboolean filterx_mapping_keys(FilterXObject *s, FilterXObject **keys);

FILTERX_DECLARE_TYPE(mapping);

static inline gboolean
filterx_mapping_normalize_key_as_string(FilterXObject *key, const gchar **key_string, gsize *key_len, const gchar **error)
{
  if (!key)
    {
      *error = "Key is mandatory";
      return FALSE;
    }

  if (!filterx_object_extract_string_ref(key, key_string, key_len))
    {
      *error = "Key must be a string";
      return FALSE;
    }
  return TRUE;
}

static inline gboolean
filterx_mapping_normalize_key(FilterXObject *key, const gchar **error)
{
  if (!key)
    {
      *error = "Key is mandatory";
      return FALSE;
    }

  if (!filterx_object_hashable(key))
    {
      *error = "Key must be hashable";
      return FALSE;
    }
  return TRUE;
}

static inline void
filterx_mapping_init_instance(FilterXMapping *self, FilterXType *type)
{
#if SYSLOG_NG_ENABLE_DEBUG
  g_assert(type->is_mutable);
#endif

  filterx_object_init_instance(&self->super, type);
}

#endif
