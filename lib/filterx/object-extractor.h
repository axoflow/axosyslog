/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 Attila Szakacs <attila.szakacs@axoflow.com>
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
#ifndef FILTERX_OBJECT_EXTRACTOR_H_INCLUDED
#define FILTERX_OBJECT_EXTRACTOR_H_INCLUDED

#include "filterx/filterx-object.h"
#include "generic-number.h"
#include "compat/json.h"

gboolean filterx_object_extract_string(FilterXObject *obj, const gchar **value, gsize *len);
gboolean filterx_object_extract_bytes(FilterXObject *obj, const gchar **value, gsize *len);
gboolean filterx_object_extract_protobuf(FilterXObject *obj, const gchar **value, gsize *len);
gboolean filterx_object_extract_boolean(FilterXObject *obj, gboolean *value);
gboolean filterx_object_extract_integer(FilterXObject *obj, gint64 *value);
gboolean filterx_object_extract_double(FilterXObject *obj, gdouble *value);
gboolean filterx_object_extract_generic_number(FilterXObject *obj, GenericNumber *value);
gboolean filterx_object_extract_datetime(FilterXObject *obj, UnixTime *value);
gboolean filterx_object_extract_null(FilterXObject *obj);
gboolean filterx_object_extract_json_array(FilterXObject *obj, struct json_object **value);
gboolean filterx_object_extract_json_object(FilterXObject *obj, struct json_object **value);

#endif
