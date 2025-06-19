/*
 * Copyright (c) 2002-2013 Balabit
 * Copyright (c) 1998-2013 Balázs Scheidler
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
#ifndef STATUS_PUBLISHER_H_INCLUDED
#define STATUS_PUBLISHER_H_INCLUDED

#include "syslog-ng.h"

typedef enum _ServiceManagementType
{
  SMT_NONE,
  SMT_SYSTEMD,
  SMT_MAX
} ServiceManagementType;

void service_management_publish_status(const gchar *status);
void service_management_clear_status(void);
void service_management_indicate_readiness(void);
ServiceManagementType service_management_get_type(void);
void service_management_init(void);

#endif
