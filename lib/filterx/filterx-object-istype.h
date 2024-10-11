/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2023 Balazs Scheidler <balazs.scheidler@axoflow.com>
 * Copyright (c) 2024 László Várady
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#ifndef FILTERX_OBJECT_ISTYPE_H
#define FILTERX_OBJECT_ISTYPE_H

#include "syslog-ng.h"
#include "filterx/filterx-object.h"
#include "filterx/filterx-ref.h"

static inline gboolean
_filterx_object_is_type(FilterXObject *object, FilterXType *type)
{
  FilterXType *self_type = object->type;
  while (self_type)
    {
      if (type == self_type)
        return TRUE;
      self_type = self_type->super_type;
    }
  return FALSE;
}

static inline gboolean
filterx_object_is_type(FilterXObject *object, FilterXType *type)
{
#if SYSLOG_NG_ENABLE_DEBUG
  g_assert(!(_filterx_type_is_referenceable(type) && _filterx_object_is_type(object, &FILTERX_TYPE_NAME(ref)))
           && "filterx_ref_unwrap() must be used before comparing to mutable types");
#endif

  return _filterx_object_is_type(object, type);
}
