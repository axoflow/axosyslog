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
#ifndef FILTERX_OBJECT_IP_H_INCLUDED
#define FILTERX_OBJECT_IP_H_INCLUDED

#include "filterx-object.h"
#include "expr-function.h"
#include <netinet/in.h>

FILTERX_DECLARE_TYPE(ip);

/* NOTE: returns NULL for invalid IP addresses */
FilterXObject *filterx_ip_new_from_string(const gchar *ip_str);
FilterXObject *filterx_typecast_ip(FilterXExpr *s, FilterXObject *args[], gsize args_len);

/*
 * Address family accessors for use by subnet's is_member_of.
 * Return NULL if the object is not an ip() or the family does not match.
 */
const struct in_addr *filterx_ip_get_v4(FilterXObject *obj);
const struct in6_addr *filterx_ip_get_v6(FilterXObject *obj);

#endif
