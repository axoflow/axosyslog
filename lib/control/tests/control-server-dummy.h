/*
 * Copyright (c) 2018 Balabit
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
#ifndef CONTROL_SERVER_DUMMY_H_INLCUDED
#define CONTROL_SERVER_DUMMY_H_INLCUDED 1

#include "control/control-server.h"

void control_connection_dummy_set_input(ControlConnection *s, const gchar *request);
const gchar *control_connection_dummy_get_output(ControlConnection *s);
void control_connection_dummy_reset_output(ControlConnection *s);
ControlConnection *control_connection_dummy_new(ControlServer *server);
ControlServer *control_server_dummy_new(void);


#endif
