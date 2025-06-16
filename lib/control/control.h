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

#ifndef CONTROL_H_INCLUDED
#define CONTROL_H_INCLUDED

#include "syslog-ng.h"

typedef struct _ControlServer ControlServer;
typedef struct _ControlConnection ControlConnection;
typedef struct _ControlCommandThread ControlCommandThread;

typedef void (*ControlCommandFunc)(ControlConnection *cc, GString *, gpointer user_data, gboolean *cancelled);
typedef struct _ControlCommand
{
  const gchar *command_name;
  ControlCommandFunc func;
  gpointer user_data;
  gboolean threaded;
} ControlCommand;

#endif
