/*
 * Copyright (c) 2025 Axoflow
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

#ifndef FILTERX_OBJECT_LIST_H
#define FILTERX_OBJECT_LIST_H

#include "filterx/filterx-object.h"

FILTERX_DECLARE_TYPE(list_object);

FilterXObject *filterx_list_new(void);
FilterXObject *filterx_list_new_from_syslog_ng_list(const gchar *repr, gssize repr_len);
FilterXObject *filterx_list_new_from_args(FilterXExpr *s, FilterXObject *args[], gsize args_len);

#endif
