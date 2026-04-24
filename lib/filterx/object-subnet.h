/*
 * Copyright (c) 2026 Axoflow
 * Copyright (c) 2026 Balazs Scheidler <balazs.scheidler@axoflow.com>
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
#ifndef FILTERX_OBJECT_SUBNET_H_INCLUDED
#define FILTERX_OBJECT_SUBNET_H_INCLUDED

#include "filterx-object.h"
#include "expr-function.h"

FILTERX_DECLARE_TYPE(subnet);

/* NOTE: returns NULL for invalid CIDR values */
FilterXObject *filterx_subnet_new_from_cidr(const gchar *cidr);
FilterXObject *filterx_typecast_subnet(FilterXExpr *s, FilterXObject *args[], gsize args_len);

#endif
