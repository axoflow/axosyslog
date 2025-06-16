/*
 * Copyright (c) 2002-2010 Balabit
 * Copyright (c) 1998-2010 Balázs Scheidler
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

#ifndef CHILDREN_H_INCLUDED
#define CHILDREN_H_INCLUDED

#include "syslog-ng.h"
#include <sys/types.h>

void child_manager_register(pid_t pid, void (*callback)(pid_t, int, gpointer), gpointer user_data,
                            GDestroyNotify user_data_destroy);
void child_manager_register_external_sigchld_handler(void (*callback)(int));
void child_manager_unregister(pid_t pid);
void child_manager_sigchild(pid_t pid, int status);

void child_manager_init(void);
void child_manager_deinit(void);

#endif
