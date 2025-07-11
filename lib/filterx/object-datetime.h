/*
 * Copyright (c) 2024 Axoflow
 * Copyright (c) 2024 Tamás Kosztyu <tamas.kosztyu@axoflow.com>
 * Copyright (c) 2023 Balazs Scheidler <balazs.scheidler@axoflow.com>
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
#ifndef FILTERX_OBJECT_DATETIME_H_INCLUDED
#define FILTERX_OBJECT_DATETIME_H_INCLUDED

#include "filterx-object.h"
#include "filterx/expr-function.h"

FILTERX_DECLARE_TYPE(datetime);

#define datefmt_isodate "%Y-%m-%dT%H:%M:%S%z"

FilterXObject *filterx_datetime_new(const UnixTime *ut);
UnixTime filterx_datetime_get_value(FilterXObject *s);
FilterXObject *filterx_typecast_datetime(FilterXExpr *s, FilterXObject *args[], gsize args_len);
FilterXObject *filterx_typecast_datetime_isodate(FilterXExpr *, FilterXObject *args[], gsize args_len);
FilterXExpr *filterx_function_strptime_new(FilterXFunctionArgs *args, GError **error);
FilterXExpr *filterx_function_strftime_new(FilterXFunctionArgs *args, GError **error);

gboolean datetime_repr(const UnixTime *ut, GString *repr);
gboolean datetime_str(const UnixTime *ut, GString *repr);
gboolean datetime_format_json(const UnixTime *ut, GString *repr);

void filterx_datetime_global_init(void);
void filterx_datetime_global_deinit(void);

#endif
