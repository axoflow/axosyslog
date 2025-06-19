/*
 * Copyright (c) 2025 Axoflow
 * Copyright (c) 2024 László Várady
 * Copyright (c) 2024 Attila Szakacs
 * Copyright (c) 2025 Balazs Scheidler <balazs.scheidler@axoflow.com>
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

#ifndef FILTERX_JSON_REPR_H_INCLUDED
#define FILTERX_JSON_REPR_H_INCLUDED

#include "filterx/filterx-object.h"

/* C API */
FilterXObject *filterx_object_from_json_object(struct json_object *jso, GError **error);
FilterXObject *filterx_object_from_json(const gchar *repr, gssize repr_len, GError **error);
gboolean filterx_object_to_json(FilterXObject *o, GString *repr);

/* exported filterx functions */
FilterXObject *filterx_format_json_call(FilterXExpr *s, FilterXObject *args[], gsize args_len);
FilterXObject *filterx_parse_json_call(FilterXExpr *s, FilterXObject *args[], gsize args_len);

#endif
