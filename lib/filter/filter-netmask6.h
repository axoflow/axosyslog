/*
 * Copyright (c) 2014 Balabit
 * Copyright (c) 2014 Zoltan Fried
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

#ifndef FILTER_NETMASK6_H_INCLUDED
#define FILTER_NETMASK6_H_INCLUDED

#include "filter-expr.h"

FilterExprNode *filter_netmask6_new(const gchar *cidr);
void get_network_address(const struct in6_addr *address, int prefix, struct in6_addr *network);

#endif
